// Adapted from the supplied laboratory starter, graphics_internal.cpp.
// Keeps its Context, FrameData and two command-buffer rendering architecture.
#include "graphics_internal.hpp"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <VkBootstrap.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4100 4189 4324)
#endif
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <imgui.h>
#include <backends/imgui_impl_vulkan.h>

namespace graphics::internal {

Context context;

namespace {

GLFWwindow* window = nullptr;
vkb::Instance instance;
VkSurfaceKHR surface = VK_NULL_HANDLE;
VkQueue presentQueue = VK_NULL_HANDLE;
uint32_t presentQueueIndex = 0;

VkSwapchainKHR swapchain = VK_NULL_HANDLE;
VkColorSpaceKHR swapchainColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
std::vector<VkImageView> swapchainViews;
uint32_t currentImage = UINT32_MAX;
bool resizeRequired = false;
bool framePrepared = false;
bool imguiInitialized = false;

VkFormat depthFormat = VK_FORMAT_UNDEFINED;
VkImage depthImage = VK_NULL_HANDLE;
VmaAllocation depthAllocation = VK_NULL_HANDLE;
VkImageView depthView = VK_NULL_HANDLE;
std::vector<VkFramebuffer> framebuffers;

VkSemaphore imageAvailable = VK_NULL_HANDLE;
std::vector<VkSemaphore> imageFinished;
VkFence frameFence = VK_NULL_HANDLE;
VkCommandPool commandPool = VK_NULL_HANDLE;
VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
VkRenderPass imguiRenderPass = VK_NULL_HANDLE;
std::vector<VkFramebuffer> imguiFramebuffers;
VkCommandPool imguiCommandPool = VK_NULL_HANDLE;
VkCommandBuffer imguiCommandBuffer = VK_NULL_HANDLE;

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS)
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) + ")");
}

void checkImGuiResult(VkResult result) {
    if (result < 0) check(result, "ImGui Vulkan operation");
}

VkFormat selectDepthFormat() {
    const VkFormat candidates[] = {VK_FORMAT_D24_UNORM_S8_UINT,
                                   VK_FORMAT_D32_SFLOAT_S8_UINT,
                                   VK_FORMAT_D32_SFLOAT};
    for (VkFormat format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(context.physical_device, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return format;
    }
    throw std::runtime_error("No supported depth attachment format");
}

void destroySwapchainResources() {
    for (auto framebuffer : imguiFramebuffers) vkDestroyFramebuffer(context.device, framebuffer, nullptr);
    imguiFramebuffers.clear();
    for (auto framebuffer : framebuffers) vkDestroyFramebuffer(context.device, framebuffer, nullptr);
    framebuffers.clear();
    for (auto semaphore : imageFinished) vkDestroySemaphore(context.device, semaphore, nullptr);
    imageFinished.clear();
    vkDestroyImageView(context.device, depthView, nullptr);
    depthView = VK_NULL_HANDLE;
    if (depthImage) vmaDestroyImage(context.allocator, depthImage, depthAllocation);
    depthImage = VK_NULL_HANDLE;
    depthAllocation = VK_NULL_HANDLE;
    for (auto view : swapchainViews) vkDestroyImageView(context.device, view, nullptr);
    swapchainViews.clear();
    vkDestroySwapchainKHR(context.device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
}

void createRenderPasses() {
    VkAttachmentDescription attachments[2]{};
    attachments[0].format = context.swapchain_format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = depthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    const VkAttachmentReference colorAttachment{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthAttachment{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachment;
    subpass.pDepthStencilAttachment = &depthAttachment;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pass.attachmentCount = 2;
    pass.pAttachments = attachments;
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    check(vkCreateRenderPass(context.device, &pass, nullptr, &context.render_pass), "Create scene render pass");

    // The second pass overlays ImGui and transitions the image for presentation.
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    subpass.pDepthStencilAttachment = nullptr;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
    pass.attachmentCount = 1;
    check(vkCreateRenderPass(context.device, &pass, nullptr, &imguiRenderPass), "Create ImGui render pass");
}

void createDepthAndFramebuffers() {
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = depthFormat;
    image.extent = {context.swapchain_extent.width, context.swapchain_extent.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo allocation{};
    allocation.usage = VMA_MEMORY_USAGE_AUTO;
    check(vmaCreateImage(context.allocator, &image, &allocation, &depthImage, &depthAllocation, nullptr),
          "Allocate depth buffer");

    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = depthImage;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = depthFormat;
    view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view.subresourceRange.levelCount = 1;
    view.subresourceRange.layerCount = 1;
    check(vkCreateImageView(context.device, &view, nullptr, &depthView), "Create depth view");

    framebuffers.resize(swapchainViews.size(), VK_NULL_HANDLE);
    imguiFramebuffers.resize(swapchainViews.size(), VK_NULL_HANDLE);
    // A presentation-completion semaphore belongs to an image, not a CPU frame.
    // Recreate the whole vector, because resize can change the image count.
    imageFinished.resize(swapchainViews.size(), VK_NULL_HANDLE);
    for (std::size_t i = 0; i < swapchainViews.size(); ++i) {
        const VkImageView attachments[]{swapchainViews[i], depthView};
        VkFramebufferCreateInfo framebuffer{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebuffer.renderPass = context.render_pass;
        framebuffer.attachmentCount = 2;
        framebuffer.pAttachments = attachments;
        framebuffer.width = context.swapchain_extent.width;
        framebuffer.height = context.swapchain_extent.height;
        framebuffer.layers = 1;
        check(vkCreateFramebuffer(context.device, &framebuffer, nullptr, &framebuffers[i]), "Create scene framebuffer");
        framebuffer.renderPass = imguiRenderPass;
        framebuffer.attachmentCount = 1;
        check(vkCreateFramebuffer(context.device, &framebuffer, nullptr, &imguiFramebuffers[i]), "Create ImGui framebuffer");
        const VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(context.device, &semaphore, nullptr, &imageFinished[i]), "Create image completion semaphore");
    }
}

void replaceSwapchain(uint32_t width, uint32_t height) {
    vkb::SwapchainBuilder builder(context.physical_device, context.device, surface,
                                  context.graphics_queue_index, presentQueueIndex);
    builder.set_desired_extent(width, height)
           .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
           .set_desired_min_image_count(2)
           .use_default_image_usage_flags()
           .set_old_swapchain(swapchain);
    if (context.swapchain_format != VK_FORMAT_UNDEFINED)
        builder.set_desired_format({context.swapchain_format, swapchainColorSpace});
    else {
        // ImGui's palette and our scene texture already contain display colors.
        // Prefer UNORM so the swapchain does not encode them as sRGB a second time.
        builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
               .add_fallback_format({VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR});
    }
    auto result = builder.build();
    if (!result) throw std::runtime_error("Create swapchain: " + result.error().message());
    auto replacement = result.value();
    auto views = replacement.get_image_views();
    if (!views) {
        vkDestroySwapchainKHR(context.device, replacement.swapchain, nullptr);
        throw std::runtime_error("Get swapchain views: " + views.error().message());
    }
    if (context.render_pass && replacement.image_format != context.swapchain_format) {
        for (auto view : views.value()) vkDestroyImageView(context.device, view, nullptr);
        vkDestroySwapchainKHR(context.device, replacement.swapchain, nullptr);
        throw std::runtime_error("Display surface format changed; restart the application");
    }
    destroySwapchainResources();
    swapchain = replacement.swapchain;
    context.swapchain_format = replacement.image_format;
    context.swapchain_extent = replacement.extent;
    swapchainColorSpace = replacement.color_space;
    swapchainViews = std::move(views.value());
    currentImage = UINT32_MAX;
}

void rebuildSwapchain() {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) return;
    check(vkDeviceWaitIdle(context.device), "Wait for swapchain resize");
    replaceSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    createDepthAndFramebuffers();
    // With one frame in flight, ImGui's internal vertex-buffer ring remains
    // valid independently of the swapchain's new number of images.
    ImGui_ImplVulkan_SetMinImageCount(2);
    resizeRequired = false;
}

void createCommandBuffer(VkCommandPool& pool, VkCommandBuffer& buffer) {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.graphics_queue_index;
    check(vkCreateCommandPool(context.device, &poolInfo, nullptr, &pool), "Create command pool");
    VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocation.commandPool = pool;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(context.device, &allocation, &buffer), "Allocate command buffer");
}

void initializeImGui() {
    // ImGui 1.91 uses a COMBINED_IMAGE_SAMPLER for each font/registered texture.
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo pool{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pool.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool.maxSets = 16;
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &size;
    check(vkCreateDescriptorPool(context.device, &pool, nullptr, &imguiDescriptorPool), "Create ImGui descriptor pool");
    createCommandBuffer(imguiCommandPool, imguiCommandBuffer);

    ImGui_ImplVulkan_InitInfo init{};
    init.ApiVersion = instance.api_version;
    init.Instance = instance.instance;
    init.PhysicalDevice = context.physical_device;
    init.Device = context.device;
    init.QueueFamily = context.graphics_queue_index;
    init.Queue = context.graphics_queue;
    init.DescriptorPool = imguiDescriptorPool;
    init.RenderPass = imguiRenderPass;
    init.MinImageCount = 2;
    init.ImageCount = static_cast<uint32_t>(swapchainViews.size());
    init.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    init.CheckVkResultFn = checkImGuiResult;
    if (!ImGui_ImplVulkan_Init(&init)) throw std::runtime_error("Initialize ImGui Vulkan backend");
    imguiInitialized = true;
    // ImGui_ImplVulkan_NewFrame(), called by main, uploads the font atlas.
}

void drawImGui() {
    check(vkResetCommandBuffer(imguiCommandBuffer, 0), "Reset ImGui command buffer");
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(imguiCommandBuffer, &begin), "Begin ImGui commands");
    VkRenderPassBeginInfo pass{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    pass.renderPass = imguiRenderPass;
    pass.framebuffer = imguiFramebuffers.at(currentImage);
    pass.renderArea.extent = context.swapchain_extent;
    vkCmdBeginRenderPass(imguiCommandBuffer, &pass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), imguiCommandBuffer);
    vkCmdEndRenderPass(imguiCommandBuffer);
    check(vkEndCommandBuffer(imguiCommandBuffer), "End ImGui commands");
}

} // namespace

bool initialize(GLFWwindow* appWindow) {
    window = appWindow;
    try {
        uint32_t extensionCount = 0;
        const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
        if (!extensions || !extensionCount) throw std::runtime_error("GLFW cannot find Vulkan surface extensions");
        // Pass the linked Vulkan entry point explicitly. On macOS this is
        // MoltenVK, so a separately installed Vulkan loader is unnecessary.
        vkb::InstanceBuilder builder(vkGetInstanceProcAddr);
        builder.set_app_name("Computer Graphics Lab 2 — Camera and Lighting")
               .require_api_version(1, 1, 0)
               .enable_extensions(extensionCount, extensions)
               .request_validation_layers()
               .use_default_debug_messenger();
        // vk-bootstrap 1.4.321 enables portability_enumeration and its instance
        // flag when available; PhysicalDeviceSelector enables portability_subset.
        auto instanceResult = builder.build();
        if (!instanceResult) throw std::runtime_error("Create Vulkan instance: " + instanceResult.error().message());
        instance = instanceResult.value();
        check(glfwCreateWindowSurface(instance.instance, window, nullptr, &surface), "Create GLFW Vulkan surface");

        vkb::PhysicalDeviceSelector selector(instance, surface);
        auto physicalResult = selector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete).require_present().select();
        if (!physicalResult) throw std::runtime_error("Select GPU: " + physicalResult.error().message());
        auto physicalDevice = physicalResult.value();
        context.physical_device = physicalDevice.physical_device;
        depthFormat = selectDepthFormat();
        vkb::DeviceBuilder deviceBuilder(physicalDevice);
        auto deviceResult = deviceBuilder.build();
        if (!deviceResult) throw std::runtime_error("Create Vulkan device: " + deviceResult.error().message());
        auto device = deviceResult.value();
        context.device = device.device;
        auto graphics = device.get_queue(vkb::QueueType::graphics);
        auto graphicsIndex = device.get_queue_index(vkb::QueueType::graphics);
        auto present = device.get_queue(vkb::QueueType::present);
        auto presentIndex = device.get_queue_index(vkb::QueueType::present);
        if (!graphics || !graphicsIndex || !present || !presentIndex)
            throw std::runtime_error("Missing graphics/presentation queue");
        context.graphics_queue = graphics.value();
        context.graphics_queue_index = graphicsIndex.value();
        presentQueue = present.value();
        presentQueueIndex = presentIndex.value();

        VmaVulkanFunctions functions{};
        functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        VmaAllocatorCreateInfo allocator{};
        allocator.physicalDevice = context.physical_device;
        allocator.device = context.device;
        allocator.instance = instance.instance;
        allocator.vulkanApiVersion = VK_API_VERSION_1_1;
        allocator.pVulkanFunctions = &functions;
        check(vmaCreateAllocator(&allocator, &context.allocator), "Create Vulkan Memory Allocator");

        int width = 0, height = 0;
        glfwGetFramebufferSize(window, &width, &height);
        if (width <= 0 || height <= 0) throw std::runtime_error("Cannot initialize a zero-size window");
        replaceSwapchain(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        createRenderPasses();
        createDepthAndFramebuffers();
        const VkSemaphoreCreateInfo semaphore{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        check(vkCreateSemaphore(context.device, &semaphore, nullptr, &imageAvailable), "Create acquire semaphore");
        VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        check(vkCreateFence(context.device, &fence, nullptr, &frameFence), "Create frame fence");
        createCommandBuffer(commandPool, commandBuffer);
        initializeImGui();
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Graphics initialization: " << error.what() << '\n';
        shutdown();
        return false;
    }
}

void shutdown() {
    if (context.device) {
        vkDeviceWaitIdle(context.device);
        if (imguiInitialized) ImGui_ImplVulkan_Shutdown();
        imguiInitialized = false;
        destroySwapchainResources();
        vkDestroyCommandPool(context.device, imguiCommandPool, nullptr);
        vkDestroyDescriptorPool(context.device, imguiDescriptorPool, nullptr);
        vkDestroyRenderPass(context.device, imguiRenderPass, nullptr);
        vkDestroyCommandPool(context.device, commandPool, nullptr);
        vkDestroyFence(context.device, frameFence, nullptr);
        vkDestroySemaphore(context.device, imageAvailable, nullptr);
        vkDestroyRenderPass(context.device, context.render_pass, nullptr);
        if (context.allocator) vmaDestroyAllocator(context.allocator);
        vkDestroyDevice(context.device, nullptr);
    }
    if (surface && instance.instance) vkDestroySurfaceKHR(instance.instance, surface, nullptr);
    if (instance.instance) vkb::destroy_instance(instance);
    context = {};
    instance = {};
    surface = VK_NULL_HANDLE;
    presentQueue = VK_NULL_HANDLE;
    imageAvailable = VK_NULL_HANDLE;
    frameFence = VK_NULL_HANDLE;
    commandPool = VK_NULL_HANDLE;
    commandBuffer = VK_NULL_HANDLE;
    imguiDescriptorPool = VK_NULL_HANDLE;
    imguiRenderPass = VK_NULL_HANDLE;
    imguiCommandPool = VK_NULL_HANDLE;
    imguiCommandBuffer = VK_NULL_HANDLE;
    framePrepared = false;
    resizeRequired = false;
    window = nullptr;
}

void resize(uint32_t, uint32_t) {
    // Query the actual framebuffer dimensions at rebuild time (Retina-aware).
    // A zero-size notification is retained until the window is restored.
    resizeRequired = true;
}

FrameData prepare() {
    framePrepared = false;
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    if (width <= 0 || height <= 0) return {};
    check(vkWaitForFences(context.device, 1, &frameFence, VK_TRUE, UINT64_MAX), "Wait for previous frame");
    if (resizeRequired) rebuildSwapchain();

    // Acquisition can become out-of-date between GLFW polling and this call.
    // Retry once; keep the fence signaled when no frame will be submitted.
    for (int attempt = 0; attempt < 2; ++attempt) {
        const VkResult result = vkAcquireNextImageKHR(context.device, swapchain, UINT64_MAX,
                                                      imageAvailable, VK_NULL_HANDLE, &currentImage);
        if (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) {
            if (result == VK_SUBOPTIMAL_KHR) resizeRequired = true;
            framePrepared = true;
            return {framebuffers.at(currentImage), commandBuffer};
        }
        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            resizeRequired = true;
            glfwGetFramebufferSize(window, &width, &height);
            if (width <= 0 || height <= 0) return {};
            rebuildSwapchain();
            continue;
        }
        check(result, "Acquire swapchain image");
    }
    return {};
}

void submitAndPresent() {
    if (!framePrepared) return;
    drawImGui();
    const VkPipelineStageFlags stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkCommandBuffer buffers[]{commandBuffer, imguiCommandBuffer};
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable;
    submit.pWaitDstStageMask = &stage;
    submit.commandBufferCount = 2;
    submit.pCommandBuffers = buffers;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &imageFinished.at(currentImage);
    // Reset only when submitting. Resetting before a failed acquire can deadlock
    // the next frame forever because nobody would signal the fence.
    check(vkResetFences(context.device, 1, &frameFence), "Reset frame fence");
    check(vkQueueSubmit(context.graphics_queue, 1, &submit, frameFence), "Submit frame");
    framePrepared = false;

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &imageFinished.at(currentImage);
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain;
    present.pImageIndices = &currentImage;
    const VkResult result = vkQueuePresentKHR(presentQueue, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
        resizeRequired = true;
    else
        check(result, "Present swapchain image");
}

} // namespace graphics::internal
