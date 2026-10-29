#include "renderer.hpp"
#include "imgui_impl_vulkan.h"
#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
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

void SceneRenderer::initialize(VkPhysicalDevice physical, VkDevice device) {
    physical_ = physical;
    device_ = device;
    VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, MaxObjects};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.maxSets = MaxObjects;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(device_, &pool, nullptr, &pool_), "Create object descriptor pool");

    auto mesh = cg::makeCylinder(50);
    indexCount_ = static_cast<uint32_t>(mesh.indices.size());
    vertices_ = makeBuffer(mesh.vertices.size() * sizeof(cg::Vertex), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
    indices_ = makeBuffer(mesh.indices.size() * sizeof(uint32_t), VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    std::memcpy(vertices_.mapped, mesh.vertices.data(), mesh.vertices.size() * sizeof(cg::Vertex));
    std::memcpy(indices_.mapped, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo layout{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    check(vkCreateDescriptorSetLayout(device_, &layout, nullptr, &setLayout_), "Create descriptor layout");

    for (uint32_t i = 0; i < MaxObjects; ++i) {
        uniforms_[i] = makeBuffer(sizeof(Uniform), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        VkDescriptorSetAllocateInfo allocate{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        allocate.descriptorPool = pool_;
        allocate.descriptorSetCount = 1;
        allocate.pSetLayouts = &setLayout_;
        check(vkAllocateDescriptorSets(device_, &allocate, &objectSets_[i]), "Allocate object descriptor");
        VkDescriptorBufferInfo buffer{uniforms_[i].buffer, 0, sizeof(Uniform)};
        VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        write.dstSet = objectSets_[i];
        write.dstBinding = 0;
        write.descriptorCount = 1;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.pBufferInfo = &buffer;
        vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    }
    VkSamplerCreateInfo sampler{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxLod = 1;
    check(vkCreateSampler(device_, &sampler, nullptr, &sampler_), "Create sampler");
    createRenderPass();
    createPipeline();
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

void SceneRenderer::createPipeline() {
    VkShaderModule vertex = loadShader(std::string(CG_SHADER_DIR) + "/cylinder.vert.spv");
    VkShaderModule fragment = loadShader(std::string(CG_SHADER_DIR) + "/cylinder.frag.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    for (auto& stage : stages) {
        stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.pName = "main";
    }
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    VkVertexInputBindingDescription binding{0, sizeof(cg::Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attributes[] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(cg::Vertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(cg::Vertex, color)},
    };
    VkPipelineVertexInputStateCreateInfo input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    input.vertexBindingDescriptionCount = 1;
    input.pVertexBindingDescriptions = &binding;
    input.vertexAttributeDescriptionCount = 2;
    input.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_BACK_BIT;
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
    VkPipelineLayoutCreateInfo layout{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.setLayoutCount = 1;
    layout.pSetLayouts = &setLayout_;
    check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_), "Create pipeline layout");
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
    VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_);
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
    check(result, "Create cylinder pipeline");
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

void SceneRenderer::update(const cg::Scene& scene, const cg::Mat4& view, const cg::Mat4& projection,
                           uint32_t objectCount) {
    // The starter's prepare() has waited for the previous frame's fence.
    // Consequently these mapped buffers cannot still be read by the GPU.
    objectCount_ = std::min({objectCount, MaxObjects, static_cast<uint32_t>(scene.objects.size())});
    for (uint32_t i = 0; i < objectCount_; ++i) {
        Uniform uniform{};
        uniform.mvp = projection * view * scene.modelMatrix(i);
        const auto& tint = scene.objects[i].tint;
        uniform.tint[0] = tint.x;
        uniform.tint[1] = tint.y;
        uniform.tint[2] = tint.z;
        uniform.tint[3] = 1;
        std::memcpy(uniforms_[i].mapped, &uniform, sizeof(uniform));
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
    for (uint32_t i = 0; i < objectCount_; ++i) {
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0,
                                1, &objectSets_[i], 0, nullptr);
        vkCmdDrawIndexed(command, indexCount_, 1, 0, 0, 0);
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
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroySampler(device_, sampler_, nullptr);
    vkDestroyDescriptorPool(device_, pool_, nullptr);
    vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    for (auto& uniform : uniforms_) destroy(uniform);
    destroy(indices_);
    destroy(vertices_);
    device_ = VK_NULL_HANDLE;
}
