#pragma once

#include "cylinder.hpp"
#include "scene.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <string>
#include <vector>

// Vulkan renders into an offscreen color/depth target displayed by ImGui.
class SceneRenderer {
public:
    void initialize(VkPhysicalDevice physical, VkDevice device);
    void resize(uint32_t width, uint32_t height);
    void update(const cg::Scene& scene, const cg::Mat4& view, const cg::Mat4& projection);
    void record(VkCommandBuffer commandBuffer);
    void shutdown();
    VkDescriptorSet texture() const { return textureSet_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t indexCount() const { return indexCount_; }

private:
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize size = 0;
    };
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct MarkerVertex { float position[3], color[3]; };
    // Each field occupies complete 16-byte slots, matching GLSL std140/std430.
    struct alignas(16) SceneUniform {
        cg::Mat4 viewProjection;
        cg::Vec4 cameraPosition, ambient, directionalDirection, directionalColorIntensity;
        std::array<uint32_t, 4> lightCounts{};
    };
    struct alignas(16) ModelUniform {
        cg::Mat4 model;
        cg::Vec4 albedo, specularShininess;
    };
    struct alignas(16) PointData { cg::Vec4 position, colorIntensity; };
    struct alignas(16) SpotData { cg::Vec4 position, directionInner, colorIntensity, outer; };
    static_assert(sizeof(SceneUniform) == 144 && sizeof(ModelUniform) == 96);
    static_assert(sizeof(PointData) == 32 && sizeof(SpotData) == 64);

    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags properties) const;
    Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    bool ensureBuffer(Buffer& buffer, VkDeviceSize required, VkBufferUsageFlags usage,
                      VkDeviceSize maximum);
    void writeSceneBuffer(uint32_t binding, const Buffer& buffer, VkDescriptorType type);
    Image makeImage(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void destroy(Buffer& buffer);
    void destroy(Image& image);
    void destroyTarget();
    void createRenderPass();
    void createPipelines();
    VkPipeline createPipeline(bool unlit, VkPrimitiveTopology topology);
    VkShaderModule loadShader(const std::string& filename);
    void updateMarkers(const cg::Scene& scene);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout sceneLayout_ = VK_NULL_HANDLE, modelLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE, markerPipeline_ = VK_NULL_HANDLE,
               linePipeline_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet textureSet_ = VK_NULL_HANDLE, sceneSet_ = VK_NULL_HANDLE,
                    modelSet_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    Buffer vertices_, indices_, sceneUniform_, pointBuffer_, spotBuffer_, markerVertices_, lineVertices_;
    Buffer modelUniform_;
    Image color_, depth_;
    uint32_t width_ = 0, height_ = 0, indexCount_ = 0;
    uint32_t markerVertexCount_ = 0, lineVertexCount_ = 0;
    VkDeviceSize maxStorageRange_ = 0;
};
