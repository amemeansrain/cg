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
int selectedPoint = 0, selectedSpot = 0;
cg::Mat4 view, projection;

void cameraControls() {
    auto& camera = scene.camera;
    ImGui::TextUnformatted("Position X / Y / Z");
    ImGui::DragFloat3("##cameraPosition", &camera.position.x, .03f);
    ImGui::SliderFloat("Yaw", &camera.yawDegrees, -180, 180, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Pitch", &camera.pitchDegrees, -89, 89, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Roll", &camera.rollDegrees, -180, 180, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Field of view", &camera.fovDegrees, 15, 100, "%.0f deg", ImGuiSliderFlags_AlwaysClamp);
}

void materialControls() {
    auto& material = scene.model.material;
    ImGui::ColorEdit3("Albedo", &material.albedo.x);
    ImGui::ColorEdit3("Specular color", &material.specular.x);
    ImGui::SliderFloat("Shininess", &material.shininess, 1, 256, "%.0f", ImGuiSliderFlags_AlwaysClamp);
}

template<class Light>
void chooseLight(const char* label, std::vector<Light>& lights, int& selected) {
    selected = std::clamp(selected, 0, std::max(0, static_cast<int>(lights.size()) - 1));
    char preview[64];
    if (lights.empty()) std::snprintf(preview, sizeof(preview), "No lights");
    else std::snprintf(preview, sizeof(preview), "%s %d / %zu", label, selected + 1, lights.size());
    if (ImGui::BeginCombo("Selected", preview)) {
        for (int i = 0; i < static_cast<int>(lights.size()); ++i) {
            char item[48];
            std::snprintf(item, sizeof(item), "%s %d", label, i + 1);
            if (ImGui::Selectable(item, selected == i)) selected = i;
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Add")) {
        lights.emplace_back();
        selected = static_cast<int>(lights.size()) - 1;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(lights.empty());
    if (ImGui::Button("Remove")) {
        lights.erase(lights.begin() + selected);
        selected = std::min(selected, std::max(0, static_cast<int>(lights.size()) - 1));
    }
    ImGui::EndDisabled();
}

void lightControls() {
    if (ImGui::CollapsingHeader("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Ambient color", &scene.ambientColor.x);
        ImGui::SliderFloat("Ambient intensity", &scene.ambientIntensity, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
    }
    if (ImGui::CollapsingHeader("Directional", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& light = scene.directional;
        ImGui::TextUnformatted("Ray direction X / Y / Z");
        ImGui::DragFloat3("##directionalDirection", &light.direction.x, .01f, -1, 1, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        ImGui::ColorEdit3("Sun color", &light.color.x);
        ImGui::SliderFloat("Sun intensity", &light.intensity, 0, 5, "%.2f", ImGuiSliderFlags_AlwaysClamp);
    }
    if (ImGui::CollapsingHeader("Point lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("point");
        chooseLight("Point", scene.pointLights, selectedPoint);
        if (!scene.pointLights.empty()) {
            auto& light = scene.pointLights[selectedPoint];
            ImGui::TextUnformatted("Position X / Y / Z");
            ImGui::DragFloat3("##position", &light.position.x, .02f);
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::SliderFloat("Intensity", &light.intensity, 0, 100, "%.2f", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::PopID();
    }
    if (ImGui::CollapsingHeader("Spot lights", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("spot");
        chooseLight("Spot", scene.spotLights, selectedSpot);
        if (!scene.spotLights.empty()) {
            auto& light = scene.spotLights[selectedSpot];
            ImGui::TextUnformatted("Position X / Y / Z");
            ImGui::DragFloat3("##position", &light.position.x, .02f);
            ImGui::TextUnformatted("Ray direction X / Y / Z");
            ImGui::DragFloat3("##direction", &light.direction.x, .01f);
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::SliderFloat("Intensity", &light.intensity, 0, 150, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            if (ImGui::SliderFloat("Inner half-angle", &light.innerAngleDegrees, 0, 89, "%.1f deg", ImGuiSliderFlags_AlwaysClamp))
                light.outerAngleDegrees = std::max(light.outerAngleDegrees, light.innerAngleDegrees);
            ImGui::SliderFloat("Outer half-angle", &light.outerAngleDegrees,
                               light.innerAngleDegrees, 89, "%.1f deg", ImGuiSliderFlags_AlwaysClamp);
        }
        ImGui::PopID();
    }
}

void controls() {
    ImGui::TextUnformatted("LAB 2 / CAMERA & LIGHTING");
    ImGui::TextDisabled("Variant 8 - Cylinder, 50 segments");
    ImGui::Separator();
    ImGui::PushItemWidth(-145.f);
    if (ImGui::BeginTabBar("Settings")) {
        if (ImGui::BeginTabItem("Camera")) { cameraControls(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Material")) { materialControls(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Lights")) { lightControls(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopItemWidth();
    ImGui::Separator();
    ImGui::Checkbox("Show light sources", &scene.showLightMarkers);
}
}

bool initialize() {
    ImGui::StyleColorsDark();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.Fonts->AddFontFromFileTTF(CG_FONT_PATH, 17.f);
    auto& ctx = graphics::internal::context;
    renderer.initialize(ctx.physical_device, ctx.device);
    return true;
}

void update() {
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Lab 2 - Variant 8", nullptr, ImGuiWindowFlags_NoDecoration |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const auto available = ImGui::GetContentRegionAvail();
    const float panelWidth = std::min(390.f, available.x * .43f);
    const float sceneWidth = std::max(1.f, available.x - panelWidth - ImGui::GetStyle().ItemSpacing.x);
    const float sceneHeight = std::max(1.f, available.y);
    const ImVec2 sceneCursor = ImGui::GetCursorPos();

    ImGui::SetCursorPos({sceneCursor.x + sceneWidth + ImGui::GetStyle().ItemSpacing.x, sceneCursor.y});
    ImGui::BeginChild("Controls", {panelWidth, sceneHeight});
    controls();
    ImGui::EndChild();

    ImGui::SetCursorPos(sceneCursor);
    const auto framebufferScale = ImGui::GetIO().DisplayFramebufferScale;
    renderer.resize(static_cast<uint32_t>(sceneWidth * framebufferScale.x),
                    static_cast<uint32_t>(sceneHeight * framebufferScale.y));
    ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(renderer.texture())),
                 {sceneWidth, sceneHeight});
    view = scene.camera.view();
    const float aspect = static_cast<float>(renderer.width()) / renderer.height();
    projection = cg::perspectiveRH_ZO(cg::radians(scene.camera.fovDegrees), aspect, .1f, 100.f, true);
    ImGui::End();
}

void render(const graphics::internal::FrameData& fd) {
    if (!fd.command_buffer) return;
    auto& ctx = graphics::internal::context;
    renderer.update(scene, view, projection);
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
