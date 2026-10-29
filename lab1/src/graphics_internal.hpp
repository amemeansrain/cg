#pragma once

#include <cstdint>
#include <vulkan/vulkan_core.h>
#include <vk_mem_alloc.h>

struct GLFWwindow;

// Adapted from the laboratory's Vulkan starter. The application records its
// commands between prepare() and submitAndPresent(); this layer owns the window.
namespace graphics::internal {

struct Context {
    VkPhysicalDevice physical_device = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkQueue graphics_queue = VK_NULL_HANDLE;
    uint32_t graphics_queue_index = 0;
    VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchain_extent{};
    VkRenderPass render_pass = VK_NULL_HANDLE;
};

struct FrameData {
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer = VK_NULL_HANDLE;
    explicit operator bool() const { return command_buffer != VK_NULL_HANDLE; }
};

extern Context context;

bool initialize(GLFWwindow* window);
void shutdown();
void resize(uint32_t width, uint32_t height);

// Waits for the previous GPU frame, then acquires the next swapchain image.
// Empty result means the window is minimized or temporarily out of date.
FrameData prepare();
void submitAndPresent();

} // namespace graphics::internal
