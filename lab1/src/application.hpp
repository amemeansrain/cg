// Lab implementation of the application interface from vulkan-starter-app.
#pragma once
#include "graphics_internal.hpp"

namespace application {
bool initialize();
void shutdown();
void update(double time);
void render(const graphics::internal::FrameData& fd);
}
