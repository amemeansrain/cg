// Application interface adapted from Vladimir Bakharev's vulkan-starter-app.
#include "application.hpp"
#include "renderer.hpp"
#include <imgui.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace application {
namespace {
SceneRenderer renderer;
cg::Scene scene;
int objectCount = 1, selectedObject = 0;
bool perspective = true;
double lastTime = -1;
cg::Mat4 view, projection;

void initializeScene() {
    scene = cg::Scene{};
    scene.objects.resize(SceneRenderer::MaxObjects);
    scene.objects[1].position = {-2.4f, 0.f, 0.f};
    scene.objects[1].scale = {.65f, .8f, .65f};
    scene.objects[1].tint = {.38f, .85f, 1.f};
    scene.objects[2].position = {2.4f, 0.f, 0.f};
    scene.objects[2].scale = {.65f, .65f, .65f};
    scene.objects[2].tint = {1.f, .55f, .3f};
    objectCount = 1;
    selectedObject = 0;
    perspective = true;
}

void controls() {
    ImGui::Text("Projection");
    if (ImGui::RadioButton("Perspective", perspective)) perspective = true;
    ImGui::SameLine();
    if (ImGui::RadioButton("Orthographic", !perspective)) perspective = false;
    ImGui::Separator();

    ImGui::Text("Objects");
    ImGui::SliderInt("Count", &objectCount, 1, 3, "%d", ImGuiSliderFlags_AlwaysClamp);
    objectCount = std::clamp(objectCount, 1, 3);
    selectedObject = std::min(selectedObject, objectCount - 1);
    for (int i = 0; i < objectCount; ++i) {
        if (i) ImGui::SameLine();
        char label[32];
        std::snprintf(label, sizeof(label), "Cylinder %d", i + 1);
        if (ImGui::Selectable(label, selectedObject == i, 0, {92, 23})) selectedObject = i;
    }
    auto& object = scene.objects[selectedObject];
    ImGui::PushItemWidth(-1);
    ImGui::Text("Position X / Y / Z");
    ImGui::DragFloat3("##position", &object.position.x, .02f, -6.f, 6.f, "%.2f");
    ImGui::Text("Rotation X / Y / Z (degrees)");
    float degrees[] = {object.rotation.x * 180 / cg::pi, object.rotation.y * 180 / cg::pi,
                       object.rotation.z * 180 / cg::pi};
    if (ImGui::DragFloat3("##rotation", degrees, .7f, -180, 180, "%.0f"))
        object.rotation = {cg::radians(degrees[0]), cg::radians(degrees[1]), cg::radians(degrees[2])};
    ImGui::Text("Scale X / Y / Z");
    ImGui::DragFloat3("##scale", &object.scale.x, .01f, .1f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    object.scale.x = std::clamp(object.scale.x, .1f, 3.f);
    object.scale.y = std::clamp(object.scale.y, .1f, 3.f);
    object.scale.z = std::clamp(object.scale.z, .1f, 3.f);
    ImGui::Text("Color");
    ImGui::ColorEdit3("##color", &object.tint.x);
    ImGui::PopItemWidth();
    ImGui::Separator();

    ImGui::Text("Animation");
    if (ImGui::Button(scene.animation.playing ? "Pause" : "Play"))
        scene.animation.playing = !scene.animation.playing;
    ImGui::SliderFloat("Speed", &scene.animation.speed, 0.f, 3.f, "%.2fx", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Radius", &scene.animation.pathRadius, 0.f, 3.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Height", &scene.animation.pathHeight, 0.f, 1.5f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Spin", &scene.animation.rotationSpeed, 0.f, 3.f, "%.2f", ImGuiSliderFlags_AlwaysClamp);
}
}

bool initialize() {
    ImGui::StyleColorsDark();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontFromFileTTF(CG_FONT_PATH, 17.f);
    initializeScene();
    auto& ctx = graphics::internal::context;
    renderer.initialize(ctx.physical_device, ctx.device);
    return true;
}

void update(double time) {
    const double dt = lastTime < 0 ? 0 : std::clamp(time - lastTime, 0.0, .1);
    lastTime = time;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Lab 1 - Variant 8", nullptr, ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const auto available = ImGui::GetContentRegionAvail();
    const float panelWidth = std::min(350.f, available.x * .43f);
    const float sceneWidth = std::max(1.f, available.x - panelWidth - ImGui::GetStyle().ItemSpacing.x);
    const ImVec2 sceneCursor = ImGui::GetCursorPos();

    // Read the controls before calculating this frame's matrices.
    ImGui::SetCursorPos({sceneCursor.x + sceneWidth + ImGui::GetStyle().ItemSpacing.x, sceneCursor.y});
    ImGui::BeginChild("Controls", {panelWidth, available.y});
    controls();
    ImGui::EndChild();
    scene.update(dt);

    ImGui::SetCursorPos(sceneCursor);
    const auto framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    renderer.resize(static_cast<uint32_t>(sceneWidth * framebufferScale.x),
                    static_cast<uint32_t>(std::max(1.f, available.y) * framebufferScale.y));
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(renderer.texture())),
                 {sceneWidth, available.y});
    view = cg::lookAt({6.f, 4.f, 8.f}, {0, 0, 0}, {0, 1, 0});
    const float aspect = static_cast<float>(renderer.width()) / renderer.height();
    constexpr float orthoScale = 3.4f;
    projection = perspective ? cg::perspectiveRH_ZO(cg::radians(45.f), aspect, .1f, 100.f, true)
                             : cg::orthoRH_ZO(-orthoScale * aspect, orthoScale * aspect,
                                             -orthoScale, orthoScale, .1f, 100.f, true);
    ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
    if (!fd.command_buffer) return;
    auto& ctx = graphics::internal::context;
    renderer.update(scene, view, projection, objectCount);
    if (vkResetCommandBuffer(fd.command_buffer, 0) != VK_SUCCESS) throw std::runtime_error("Reset commands failed");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(fd.command_buffer, &begin) != VK_SUCCESS) throw std::runtime_error("Begin commands failed");
    renderer.record(fd.command_buffer);
    VkClearValue clear[2]{};
    clear[0].color = {{.055f, .075f, .105f, 1.f}};
    clear[1].depthStencil = {1.f, 0};
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = ctx.render_pass;
    pass.framebuffer = fd.framebuffer;
    pass.renderArea.extent = ctx.swapchain_extent;
    pass.clearValueCount = 2;
    pass.pClearValues = clear;
    vkCmdBeginRenderPass(fd.command_buffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdEndRenderPass(fd.command_buffer);
    if (vkEndCommandBuffer(fd.command_buffer) != VK_SUCCESS) throw std::runtime_error("End commands failed");
}

void shutdown() { renderer.shutdown(); }
}
