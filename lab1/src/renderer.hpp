#pragma once

#include "cylinder.hpp"
#include "scene.hpp"
#include <vulkan/vulkan.h>
#include <array>
#include <string>

// The scene is rendered into a color/depth framebuffer. ImGui displays the
// color attachment as a texture; the actual geometry is drawn by Vulkan.
class SceneRenderer {
public:
    static constexpr uint32_t MaxObjects = 3;
    void initialize(VkPhysicalDevice physical, VkDevice device);
    void resize(uint32_t width, uint32_t height);
    void update(const cg::Scene& scene, const cg::Mat4& view, const cg::Mat4& projection,
                uint32_t objectCount);
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
    };
    struct Image {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct alignas(16) Uniform {
        cg::Mat4 mvp;
        float tint[4];
    };
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags properties) const;
    Buffer makeBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    Image makeImage(VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect);
    void destroy(Buffer& buffer);
    void destroy(Image& image);
    void destroyTarget();
    void createRenderPass();
    void createPipeline();
    VkShaderModule loadShader(const std::string& filename);

    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSet textureSet_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    Buffer vertices_, indices_;
    std::array<Buffer, MaxObjects> uniforms_{};
    std::array<VkDescriptorSet, MaxObjects> objectSets_{};
    Image color_, depth_;
    uint32_t width_ = 0, height_ = 0, indexCount_ = 0, objectCount_ = 1;
};
