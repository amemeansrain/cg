cmake_minimum_required(VERSION 3.24)
project(cg_lab1 VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(CG_BUILD_TESTS "Build mathematical and cylinder mesh tests" ON)
set(CG_IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui-1.91.9b")
set(CG_GLFW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glfw-3.4")
if(NOT EXISTS "${CG_IMGUI_DIR}/imgui.cpp" OR NOT EXISTS "${CG_GLFW_DIR}/CMakeLists.txt")
  message(FATAL_ERROR "Dependencies are missing. Run scripts/bootstrap_deps.sh first.")
endif()

# GLFW creates only a native window. All graphics, including ImGui, use Vulkan.
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CG_GLFW_DIR}" "${CMAKE_BINARY_DIR}/glfw")

if(APPLE)
  set(CG_MOLTENVK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.deps/MoltenVK/MoltenVK" CACHE PATH "MoltenVK library root")
  find_path(CG_VULKAN_INCLUDE_DIR vulkan/vulkan.h
    PATHS "${CG_MOLTENVK_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
  find_library(CG_MOLTENVK_LIBRARY NAMES MoltenVK
    PATHS "${CG_MOLTENVK_ROOT}/dynamic/dylib/macOS" NO_DEFAULT_PATH REQUIRED)
  add_library(cg_vulkan SHARED IMPORTED)
  set_target_properties(cg_vulkan PROPERTIES
    IMPORTED_LOCATION "${CG_MOLTENVK_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${CG_VULKAN_INCLUDE_DIR}")
else()
  find_package(Vulkan REQUIRED)
  add_library(cg_vulkan ALIAS Vulkan::Vulkan)
endif()

set(VK_BOOTSTRAP_TEST OFF CACHE BOOL "" FORCE)
set(VK_BOOTSTRAP_INSTALL OFF CACHE BOOL "" FORCE)
if(APPLE)
  set(VK_BOOTSTRAP_VULKAN_HEADER_DIR "${CG_VULKAN_INCLUDE_DIR}" CACHE FILEPATH "" FORCE)
endif()
set(VMA_BUILD_SAMPLES OFF CACHE BOOL "" FORCE)
set(VMA_BUILD_DOCUMENTATION OFF CACHE BOOL "" FORCE)
set(VMA_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("third_party/vk-bootstrap-1.4.321" "${CMAKE_BINARY_DIR}/vk-bootstrap")
add_subdirectory("third_party/VulkanMemoryAllocator-3.4.0" "${CMAKE_BINARY_DIR}/vma")

add_library(cg_imgui STATIC
  "${CG_IMGUI_DIR}/imgui.cpp"
  "${CG_IMGUI_DIR}/imgui_draw.cpp"
  "${CG_IMGUI_DIR}/imgui_tables.cpp"
  "${CG_IMGUI_DIR}/imgui_widgets.cpp"
  "${CG_IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
  "${CG_IMGUI_DIR}/backends/imgui_impl_vulkan.cpp")
target_include_directories(cg_imgui PUBLIC "${CG_IMGUI_DIR}" "${CG_IMGUI_DIR}/backends")
target_link_libraries(cg_imgui PUBLIC glfw cg_vulkan)

find_program(CG_GLSLANG NAMES glslang glslangValidator
  HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/glslang-build/StandAlone" "$ENV{VULKAN_SDK}/bin")
if(NOT CG_GLSLANG)
  message(FATAL_ERROR "glslang is missing. Run scripts/bootstrap_deps.sh first.")
endif()
set(CG_SHADER_OUTPUTS)
foreach(shader cylinder.vert cylinder.frag)
  set(shader_out "${CMAKE_BINARY_DIR}/shaders/${shader}.spv")
  add_custom_command(OUTPUT "${shader_out}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_BINARY_DIR}/shaders"
    COMMAND "${CG_GLSLANG}" -V --target-env vulkan1.0
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}" -o "${shader_out}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}"
    COMMENT "Compiling ${shader} to SPIR-V" VERBATIM)
  list(APPEND CG_SHADER_OUTPUTS "${shader_out}")
endforeach()
add_custom_target(cg_shaders DEPENDS ${CG_SHADER_OUTPUTS})

add_executable(cg_lab1 src/main.cpp src/application.cpp src/graphics_internal.cpp src/renderer.cpp)
target_include_directories(cg_lab1 PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_include_directories(cg_lab1 SYSTEM PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/VulkanMemoryAllocator-3.4.0/include")
target_link_libraries(cg_lab1 PRIVATE cg_imgui vk-bootstrap::vk-bootstrap GPUOpen::VulkanMemoryAllocator)
target_compile_definitions(cg_lab1 PRIVATE
  CG_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"
  CG_FONT_PATH="${CG_IMGUI_DIR}/misc/fonts/Roboto-Medium.ttf")
add_dependencies(cg_lab1 cg_shaders)
if(APPLE)
  set_target_properties(cg_lab1 PROPERTIES
    BUILD_RPATH "${CG_MOLTENVK_ROOT}/dynamic/dylib/macOS")
  set(CG_MACOS_BUNDLE "${CMAKE_BINARY_DIR}/Cylinder Lab.app")
  configure_file("scripts/Info.plist.in" "${CMAKE_BINARY_DIR}/CylinderLab-Info.plist" @ONLY)
  add_custom_command(TARGET cg_lab1 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${CG_MACOS_BUNDLE}/Contents/MacOS"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "$<TARGET_FILE:cg_lab1>" "${CG_MACOS_BUNDLE}/Contents/MacOS/cg_lab1"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_BINARY_DIR}/CylinderLab-Info.plist" "${CG_MACOS_BUNDLE}/Contents/Info.plist"
    COMMENT "Preparing Cylinder Lab.app for macOS" VERBATIM)
endif()
if(MSVC)
  target_compile_options(cg_lab1 PRIVATE /W4)
else()
  target_compile_options(cg_lab1 PRIVATE -Wall -Wextra -Wpedantic -Wno-missing-field-initializers)
endif()

if(CG_BUILD_TESTS)
  enable_testing()
  add_executable(core_tests tests/core_tests.cpp)
  target_include_directories(core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  add_test(NAME core_tests COMMAND core_tests)
endif()
