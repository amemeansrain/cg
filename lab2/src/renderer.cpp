#include "renderer.hpp"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + ": VkResult=" + std::to_string(result));
}
}

uint32_t SceneRenderer::memoryType(uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    for (uint32_t i = 0; i < properties.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags)
            return i;
    throw std::runtime_error("No suitable Vulkan memory type");
}

SceneRenderer::Buffer SceneRenderer::makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer result;
    result.size = size;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    check(vkCreateBuffer(device_, &info, nullptr, &result.buffer), "Create buffer");
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(device_, result.buffer, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    check(vkAllocateMemory(device_, &allocation, nullptr, &result.memory), "Allocate buffer");
    check(vkBindBufferMemory(device_, result.buffer, result.memory, 0), "Bind buffer");
    check(vkMapMemory(device_, result.memory, 0, size, 0, &result.mapped), "Map buffer");
    return result;
}

SceneRenderer::Image SceneRenderer::makeImage(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image result;
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {width_, height_, 1};
    info.mipLevels = info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    check(vkCreateImage(device_, &info, nullptr, &result.image), "Create image");
    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(device_, result.image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    check(vkAllocateMemory(device_, &allocation, nullptr, &result.memory), "Allocate image");
    check(vkBindImageMemory(device_, result.image, result.memory, 0), "Bind image");
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = result.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = {aspect, 0, 1, 0, 1};
    check(vkCreateImageView(device_, &view, nullptr, &result.view), "Create image view");
    return result;
}

bool SceneRenderer::ensureBuffer(Buffer& buffer, VkDeviceSize required, VkBufferUsageFlags usage,
                                 VkDeviceSize maximum) {
    required = std::max<VkDeviceSize>(required, 16);
    if (required > maximum) throw std::runtime_error("Light data exceeds the GPU storage buffer limit");
    if (buffer.size >= required) return false;
    const VkDeviceSize capacity = std::min(maximum, std::max(required, buffer.size * 2));
    Buffer replacement = makeBuffer(capacity, usage);
    destroy(buffer);
    buffer = replacement;
    return true;
}

void SceneRenderer::writeSceneBuffer(uint32_t binding, const Buffer& buffer, VkDescriptorType type) {
    VkDescriptorBufferInfo info{buffer.buffer, 0, buffer.size};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = sceneSet_;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = type;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
}

void SceneRenderer::initialize(VkPhysicalDevice physical, VkDevice device) {
    physical_ = physical;
    device_ = device;
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(physical_, &properties);
    maxStorageRange_ = properties.limits.maxStorageBufferRange;
    VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
    };
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = 2;
    pool.poolSizeCount = 2;
    pool.pPoolSizes = sizes;
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &pool_), "Create scene/model descriptor pool");

    auto mesh = cg::makeCylinder(50);
    indexCount_ = static_cast<uint32_t>(mesh.indices.size());
    vertices_ = makeBuffer(mesh.vertices.size() * sizeof(cg::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    indices_ = makeBuffer(mesh.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::memcpy(vertices_.mapped, mesh.vertices.data(), mesh.vertices.size() * sizeof(cg::Vertex));
    std::memcpy(indices_.mapped, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));

    VkDescriptorSetLayoutBinding sceneBindings[3]{};
    for (uint32_t i = 0; i < 3; ++i) {
        sceneBindings[i].binding = i;
        sceneBindings[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        sceneBindings[i].descriptorCount = 1;
        sceneBindings[i].stageFlags = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 3;
    layout.pBindings = sceneBindings;
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &sceneLayout_), "Create scene descriptor layout");
    VkDescriptorSetLayoutBinding modelBinding{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1,
                                             VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    layout.bindingCount = 1;
    layout.pBindings = &modelBinding;
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &modelLayout_), "Create model descriptor layout");
    VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = pool_;
    allocate.descriptorSetCount = 1;
    allocate.pSetLayouts = &sceneLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &sceneSet_), "Allocate scene descriptor");
    sceneUniform_ = makeBuffer(sizeof(SceneUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    pointBuffer_ = makeBuffer(sizeof(PointData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    spotBuffer_ = makeBuffer(sizeof(SpotData), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
    writeSceneBuffer(0, sceneUniform_, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writeSceneBuffer(1, pointBuffer_, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    writeSceneBuffer(2, spotBuffer_, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);

    modelUniform_ = makeBuffer(sizeof(ModelUniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    allocate.pSetLayouts = &modelLayout_;
    check(vkAllocateDescriptorSets(device_, &allocate, &modelSet_), "Allocate model descriptor");
    VkDescriptorBufferInfo info{modelUniform_.buffer, 0, sizeof(ModelUniform)};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = modelSet_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &info;
    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 1;
    check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Create sampler");
    createRenderPass();
    createPipelines();
    resize(1000, 800);
}

void SceneRenderer::createRenderPass() {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(physical_, depthFormat_, &properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
        depthFormat_ = VK_FORMAT_D16_UNORM;
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    attachments[1] = attachments[0];
    attachments[1].format = depthFormat_;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    VkSubpassDependency dependencies[2]{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 2;
    pass.pAttachments = attachments;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 2;
    pass.pDependencies = dependencies;
    check(vkCreateRenderPass(device_, &pass, nullptr, &renderPass_), "Create scene render pass");
}

VkShaderModule SceneRenderer::loadShader(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("Cannot open shader " + filename);
    const auto size = static_cast<size_t>(file.tellg());
    if (size == 0 || size % 4 != 0) throw std::runtime_error("Invalid SPIR-V file " + filename);
    std::vector<uint32_t> code(size / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(code.data()), size);
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = code.data();
    VkShaderModule module;
    check(vkCreateShaderModule(device_, &info, nullptr, &module), "Create shader module");
    return module;
}

void SceneRenderer::createPipelines() {
    VkDescriptorSetLayout layouts[] = {sceneLayout_, modelLayout_};
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 2;
    layout.pSetLayouts = layouts;
    check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_), "Create pipeline layout");
    pipeline_ = createPipeline(false, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    markerPipeline_ = createPipeline(true, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    linePipeline_ = createPipeline(true, VK_PRIMITIVE_TOPOLOGY_LINE_LIST);
}

VkPipeline SceneRenderer::createPipeline(bool unlit, VkPrimitiveTopology topology) {
    const std::string prefix = std::string(CG_SHADER_DIR) + (unlit ? "/marker" : "/cylinder");
    VkShaderModule vertex = loadShader(prefix + ".vert.spv");
    VkShaderModule fragment = loadShader(prefix + ".frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    VkVertexInputBindingDescription binding{0,
        static_cast<uint32_t>(unlit ? sizeof(MarkerVertex) : sizeof(cg::Vertex)), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, static_cast<uint32_t>(
            unlit ? offsetof(MarkerVertex, color) : offsetof(cg::Vertex, normal))},
    };
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = topology;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = unlit ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    VkDynamicState states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = states;
    VkGraphicsPipelineCreateInfo pipeline{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeline.stageCount = 2;
    pipeline.pStages = stages;
    pipeline.pVertexInputState = &input;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = pipelineLayout_;
    pipeline.renderPass = renderPass_;
    VkPipeline output = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &output);
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
    check(result, "Create scene pipeline");
    return output;
}

void SceneRenderer::resize(uint32_t width, uint32_t height) {
    width = std::max(1u, width);
    height = std::max(1u, height);
    if (width_ == width && height_ == height) return;
    check(vkDeviceWaitIdle(device_), "Wait before resize");
    destroyTarget();
    width_ = width;
    height_ = height;
    color_ = makeImage(VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT);
    depth_ = makeImage(depthFormat_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageView attachments[] = {color_.view, depth_.view};
    VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    info.renderPass = renderPass_;
    info.attachmentCount = 2;
    info.pAttachments = attachments;
    info.width = width_;
    info.height = height_;
    info.layers = 1;
    check(vkCreateFramebuffer(device_, &info, nullptr, &framebuffer_), "Create scene framebuffer");
    textureSet_ = ImGui_ImplVulkan_AddTexture(sampler_, color_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

namespace {
cg::Vec4 pack(cg::Vec3 v, float w = 0) { return {v.x, v.y, v.z, w}; }
cg::Vec3 safeDirection(cg::Vec3 v) {
    const float magnitude = cg::length(v);
    return magnitude > 1e-6f && std::isfinite(magnitude) ? v / magnitude : cg::Vec3{0, -1, 0};
}
}

void SceneRenderer::update(const cg::Scene& scene, const cg::Mat4& view, const cg::Mat4& projection) {
    // prepare() has waited for the previous frame's fence before these writes.
    if (scene.pointLights.size() > maxStorageRange_ / sizeof(PointData) ||
        scene.spotLights.size() > maxStorageRange_ / sizeof(SpotData))
        throw std::runtime_error("Light count exceeds the GPU storage buffer limit");
    if (ensureBuffer(pointBuffer_, std::max<size_t>(1, scene.pointLights.size()) * sizeof(PointData),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxStorageRange_))
        writeSceneBuffer(1, pointBuffer_, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    if (ensureBuffer(spotBuffer_, std::max<size_t>(1, scene.spotLights.size()) * sizeof(SpotData),
                     VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, maxStorageRange_))
        writeSceneBuffer(2, spotBuffer_, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    auto* points = static_cast<PointData*>(pointBuffer_.mapped);
    for (size_t i = 0; i < scene.pointLights.size(); ++i) {
        const auto& light = scene.pointLights[i];
        points[i] = {pack(light.position), pack(light.color, std::max(0.f, light.intensity))};
    }
    auto* spots = static_cast<SpotData*>(spotBuffer_.mapped);
    for (size_t i = 0; i < scene.spotLights.size(); ++i) {
        const auto& light = scene.spotLights[i];
        const float outer = std::clamp(light.outerAngleDegrees, 0.f, 89.f);
        const float inner = std::clamp(light.innerAngleDegrees, 0.f, outer);
        spots[i] = {pack(light.position), pack(safeDirection(light.direction), std::cos(cg::radians(inner))),
                    pack(light.color, std::max(0.f, light.intensity)),
                    {std::cos(cg::radians(outer)), 0, 0, 0}};
    }
    SceneUniform globals{};
    globals.viewProjection = projection * view;
    globals.cameraPosition = pack(scene.camera.position, 1);
    globals.ambient = pack(scene.ambientColor, scene.ambientIntensity);
    globals.directionalDirection = pack(safeDirection(scene.directional.direction));
    globals.directionalColorIntensity = pack(scene.directional.color, scene.directional.intensity);
    globals.lightCounts = {static_cast<uint32_t>(scene.pointLights.size()),
                          static_cast<uint32_t>(scene.spotLights.size()), 0, 0};
    std::memcpy(sceneUniform_.mapped, &globals, sizeof(globals));

    const auto& material = scene.model.material;
    ModelUniform uniform{scene.model.transform, pack(material.albedo),
                         pack(material.specular, material.shininess)};
    std::memcpy(modelUniform_.mapped, &uniform, sizeof(uniform));
    updateMarkers(scene);
}

void SceneRenderer::updateMarkers(const cg::Scene& scene) {
    std::vector<MarkerVertex> triangles, lines;
    const auto markerVertex = [](cg::Vec3 p, cg::Vec3 color) {
        return MarkerVertex{{p.x, p.y, p.z}, {color.x, color.y, color.z}};
    };
    const auto cube = [&triangles, &markerVertex](cg::Vec3 center, cg::Vec3 color) {
        constexpr float size = .09f;
        constexpr int indices[] = {0,2,1,0,3,2,4,5,6,4,6,7,0,1,5,0,5,4,
                                   3,7,6,3,6,2,0,4,7,0,7,3,1,2,6,1,6,5};
        const cg::Vec3 corners[] = {{-size,-size,-size},{size,-size,-size},{size,size,-size},{-size,size,-size},
                                    {-size,-size,size},{size,-size,size},{size,size,size},{-size,size,size}};
        for (int index : indices) triangles.push_back(markerVertex(center + corners[index], color));
    };
    if (scene.showLightMarkers) {
        for (const auto& light : scene.pointLights) cube(light.position, light.color);
        for (const auto& light : scene.spotLights) cube(light.position, light.color);
    }
    if (scene.showLightMarkers) {
        const auto line = [&lines, &markerVertex](cg::Vec3 a, cg::Vec3 b) {
            lines.push_back(markerVertex(a, {1, 1, 1}));
            lines.push_back(markerVertex(b, {1, 1, 1}));
        };
        for (const auto& light : scene.spotLights) {
            const auto direction = safeDirection(light.direction);
            const auto reference = std::abs(direction.y) > .95f ? cg::Vec3{1, 0, 0} : cg::Vec3{0, 1, 0};
            const auto right = cg::normalize(cg::cross(direction, reference));
            const auto up = cg::cross(right, direction);
            constexpr float coneLength = 1.8f;
            const auto center = light.position + direction * coneLength;
            const float radius = coneLength * std::tan(cg::radians(std::clamp(light.outerAngleDegrees, 0.f, 89.f)));
            constexpr int segments = 32;
            for (int i = 0; i < segments; ++i) {
                const float a = 2 * cg::pi * i / segments, b = 2 * cg::pi * (i + 1) / segments;
                const auto p = center + (right * std::cos(a) + up * std::sin(a)) * radius;
                const auto q = center + (right * std::cos(b) + up * std::sin(b)) * radius;
                line(p, q);
                if (i % 8 == 0) line(light.position, p);
            }
            line(light.position, center);
        }
    }
    markerVertexCount_ = static_cast<uint32_t>(triangles.size());
    lineVertexCount_ = static_cast<uint32_t>(lines.size());
    if (!triangles.empty()) {
        ensureBuffer(markerVertices_, triangles.size() * sizeof(MarkerVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     std::numeric_limits<VkDeviceSize>::max() / 2);
        std::memcpy(markerVertices_.mapped, triangles.data(), triangles.size() * sizeof(MarkerVertex));
    }
    if (!lines.empty()) {
        ensureBuffer(lineVertices_, lines.size() * sizeof(MarkerVertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     std::numeric_limits<VkDeviceSize>::max() / 2);
        std::memcpy(lineVertices_.mapped, lines.data(), lines.size() * sizeof(MarkerVertex));
    }
}

void SceneRenderer::record(VkCommandBuffer command) {
    VkClearValue clear[2]{};
    clear[0].color = {{0.055f, 0.075f, 0.105f, 1.f}};
    clear[1].depthStencil = {1.f, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = renderPass_;
    begin.framebuffer = framebuffer_;
    begin.renderArea.extent = {width_, height_};
    begin.clearValueCount = 2;
    begin.pClearValues = clear;
    vkCmdBeginRenderPass(command, &begin, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport viewport{0, 0, static_cast<float>(width_), static_cast<float>(height_), 0, 1};
    VkRect2D scissor{{0, 0}, {width_, height_}};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command, 0, 1, &vertices_.buffer, &offset);
    vkCmdBindIndexBuffer(command, indices_.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                            1, &sceneSet_, 0, nullptr);
    vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 1,
                            1, &modelSet_, 0, nullptr);
    vkCmdDrawIndexed(command, indexCount_, 1, 0, 0, 0);
    if (markerVertexCount_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, markerPipeline_);
        vkCmdBindVertexBuffers(command, 0, 1, &markerVertices_.buffer, &offset);
        vkCmdDraw(command, markerVertexCount_, 1, 0, 0);
    }
    if (lineVertexCount_) {
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline_);
        vkCmdBindVertexBuffers(command, 0, 1, &lineVertices_.buffer, &offset);
        vkCmdDraw(command, lineVertexCount_, 1, 0, 0);
    }
    vkCmdEndRenderPass(command);
}

void SceneRenderer::destroy(Buffer& buffer) {
    if (buffer.mapped) vkUnmapMemory(device_, buffer.memory);
    if (buffer.buffer) vkDestroyBuffer(device_, buffer.buffer, nullptr);
    if (buffer.memory) vkFreeMemory(device_, buffer.memory, nullptr);
    buffer = {};
}
void SceneRenderer::destroy(Image& image) {
    if (image.view) vkDestroyImageView(device_, image.view, nullptr);
    if (image.image) vkDestroyImage(device_, image.image, nullptr);
    if (image.memory) vkFreeMemory(device_, image.memory, nullptr);
    image = {};
}
void SceneRenderer::destroyTarget() {
    if (textureSet_) ImGui_ImplVulkan_RemoveTexture(textureSet_);
    textureSet_ = VK_NULL_HANDLE;
    if (framebuffer_) vkDestroyFramebuffer(device_, framebuffer_, nullptr);
    framebuffer_ = VK_NULL_HANDLE;
    destroy(depth_);
    destroy(color_);
}
void SceneRenderer::shutdown() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    destroyTarget();
    vkDestroyPipeline(device_, linePipeline_, nullptr);
    vkDestroyPipeline(device_, markerPipeline_, nullptr);
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroySampler(device_, sampler_, nullptr);
    vkDestroyDescriptorPool(device_, pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, modelLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, sceneLayout_, nullptr);
    destroy(sceneUniform_);
    destroy(pointBuffer_);
    destroy(spotBuffer_);
    destroy(markerVertices_);
    destroy(lineVertices_);
    destroy(modelUniform_);
    destroy(indices_);
    destroy(vertices_);
    device_ = VK_NULL_HANDLE;
}

