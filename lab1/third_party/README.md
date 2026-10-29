# Third-party dependencies

`scripts/bootstrap_deps.sh` downloads the following fixed versions and validates
the source archives with SHA-256. Downloads and build tools stay inside the
project. Extracted dependency directories are excluded from Git.

| Component | Version | Upstream | License |
| --- | --- | --- | --- |
| Dear ImGui | 1.91.9b | https://github.com/ocornut/imgui | MIT |
| GLFW | 3.4 | https://github.com/glfw/glfw | zlib/libpng |
| vk-bootstrap | 1.4.321 | https://github.com/charles-lunarg/vk-bootstrap | MIT |
| Vulkan Memory Allocator | 3.4.0 | https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator | MIT |
| MoltenVK (macOS) | 1.4.2 | https://github.com/KhronosGroup/MoltenVK | Apache 2.0 and bundled notices |
| glslang | 15.1.0 | https://github.com/KhronosGroup/glslang | BSD-style and bundled notices |
| CMake (local macOS build tool) | 3.31.6 | https://cmake.org | BSD 3-clause |
| Roboto Medium (bundled with ImGui) | ImGui 1.91.9b copy | https://github.com/googlefonts/roboto | Apache 2.0 |

License copies for the application dependencies and font are in `licenses/`.
Original notices remain in the downloaded distributions, including MoltenVK's
packaged acknowledgements and CMake's copyright files.
