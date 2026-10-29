// Adapted from vulkan-starter-app (Copyright 2026 Vladimir Bakharev).
// Changes: macOS MoltenVK loading, lab scene, minimized-window handling
// and error reporting. See LICENSE and NOTICE.
#include "application.hpp"
#include "graphics_internal.hpp"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main() {
    GLFWwindow* window = nullptr;
    bool platformReady = false, graphicsReady = false, applicationReady = false;
    int result = EXIT_SUCCESS;
    try {
        glfwSetErrorCallback([](int error, const char* message) {
            std::fprintf(stderr, "GLFW %d: %s\n", error, message);
        });
        // The local macOS build links MoltenVK directly, without a global SDK.
        glfwInitVulkanLoader(vkGetInstanceProcAddr);
        if (!glfwInit()) throw std::runtime_error("Cannot initialize GLFW");
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(1280, 820, "CG Lab 1 | Cylinder | Variant 8", nullptr, nullptr);
        if (!window) throw std::runtime_error("Cannot create the application window");
        glfwSetWindowSizeLimits(window, 880, 620, GLFW_DONT_CARE, GLFW_DONT_CARE);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow*, int width, int height) {
            if (width > 0 && height > 0) graphics::internal::resize(width, height);
        });
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        if (!ImGui_ImplGlfw_InitForVulkan(window, true)) throw std::runtime_error("Cannot initialize ImGui GLFW backend");
        platformReady = true;
        if (!graphics::internal::initialize(window)) throw std::runtime_error("Cannot initialize Vulkan");
        graphicsReady = true;
        applicationReady = true; // shutdown() is safe after a partially completed init.
        if (!application::initialize()) throw std::runtime_error("Cannot initialize the lab");
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            int width, height;
            glfwGetFramebufferSize(window, &width, &height);
            if (width == 0 || height == 0 || glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
                glfwWaitEventsTimeout(.05);
                continue;
            }
            ImGui_ImplVulkan_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::NewFrame();
            application::update(glfwGetTime());
            ImGui::Render();
            const auto frame = graphics::internal::prepare();
            if (!frame.command_buffer) continue;
            application::render(frame);
            graphics::internal::submitAndPresent();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        result = EXIT_FAILURE;
    }
    if (applicationReady) application::shutdown();
    if (graphicsReady) graphics::internal::shutdown();
    if (platformReady) ImGui_ImplGlfw_Shutdown();
    if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
    if (window) glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
