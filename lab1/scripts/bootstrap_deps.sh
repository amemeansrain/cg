#!/usr/bin/env bash
# Downloads are pinned and kept inside this project. No global installation.
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_dir="$project_dir/.deps"
third_party_dir="$project_dir/third_party"
mkdir -p "$deps_dir/downloads" "$third_party_dir"

check_sha() {
  local expected="$1" archive="$2" actual
  if command -v shasum >/dev/null 2>&1; then
    actual="$(shasum -a 256 "$archive" | cut -d ' ' -f 1)"
  else
    actual="$(sha256sum "$archive" | cut -d ' ' -f 1)"
  fi
  [[ "$actual" == "$expected" ]]
}

fetch() {
  local url="$1" filename="$2" checksum="$3"
  local archive="$deps_dir/downloads/$filename"
  if [[ ! -f "$archive" ]] || ! check_sha "$checksum" "$archive"; then
    printf 'Downloading %s\n' "$filename"
    curl -fL --retry 3 --connect-timeout 20 --max-time 600 "$url" -o "$archive.part"
    if ! check_sha "$checksum" "$archive.part"; then
      printf 'SHA-256 mismatch: %s\n' "$filename" >&2
      exit 1
    fi
    mv "$archive.part" "$archive"
  fi
}

if [[ ! -f "$third_party_dir/imgui-1.91.9b/imgui.cpp" ]]; then
  fetch 'https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz' \
    'imgui-1.91.9b.tar.gz' '8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb'
  tar -xzf "$deps_dir/downloads/imgui-1.91.9b.tar.gz" -C "$third_party_dir"
fi
if [[ ! -f "$third_party_dir/glfw-3.4/CMakeLists.txt" ]]; then
  fetch 'https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz' \
    'glfw-3.4.tar.gz' 'c038d34200234d071fae9345bc455e4a8f2f544ab60150765d7704e08f3dac01'
  tar -xzf "$deps_dir/downloads/glfw-3.4.tar.gz" -C "$third_party_dir"
fi
if [[ ! -f "$third_party_dir/vk-bootstrap-1.4.321/src/VkBootstrap.h" ]]; then
  fetch 'https://github.com/charles-lunarg/vk-bootstrap/archive/refs/tags/v1.4.321.tar.gz' \
    'vk-bootstrap-1.4.321.tar.gz' '4d4d03523822dd684239cce45a48a14f1c4f653c870e0982ebf09d9cea3299e4'
  tar -xzf "$deps_dir/downloads/vk-bootstrap-1.4.321.tar.gz" -C "$third_party_dir"
fi
if [[ ! -f "$third_party_dir/VulkanMemoryAllocator-3.4.0/include/vk_mem_alloc.h" ]]; then
  fetch 'https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/archive/refs/tags/v3.4.0.tar.gz' \
    'vma-3.4.0.tar.gz' '822aa850c6ce77346ae96a8a1d351d52e77e85929f35363849a0a4e638e0a2a1'
  tar -xzf "$deps_dir/downloads/vma-3.4.0.tar.gz" -C "$third_party_dir"
fi

if [[ "$(uname -s)" == Darwin ]]; then
  if [[ ! -x "$deps_dir/python/cmake/data/bin/cmake" ]]; then
    python3 -m pip install --target "$deps_dir/python" --no-cache-dir 'cmake==3.31.6'
  fi
  cmake_bin="$deps_dir/python/cmake/data/bin/cmake"
  if [[ ! -f "$deps_dir/MoltenVK/MoltenVK/dynamic/dylib/macOS/libMoltenVK.dylib" ]]; then
    fetch 'https://github.com/KhronosGroup/MoltenVK/releases/download/v1.4.2/MoltenVK-macos.tar' \
      'MoltenVK-macos.tar' 'f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e'
    tar -xf "$deps_dir/downloads/MoltenVK-macos.tar" -C "$deps_dir"
  fi
else
  if ! command -v cmake >/dev/null 2>&1; then
    printf 'Install CMake 3.24+ and the Vulkan SDK before building.\n' >&2
    exit 1
  fi
  cmake_bin="$(command -v cmake)"
fi

if [[ ! -x "$deps_dir/glslang-build/StandAlone/glslang" ]] \
  && ! command -v glslang >/dev/null 2>&1 \
  && ! command -v glslangValidator >/dev/null 2>&1; then
  if [[ ! -f "$deps_dir/glslang-15.1.0/CMakeLists.txt" ]]; then
    fetch 'https://github.com/KhronosGroup/glslang/archive/refs/tags/15.1.0.tar.gz' \
      'glslang-15.1.0.tar.gz' '4bdcd8cdb330313f0d4deed7be527b0ac1c115ff272e492853a6e98add61b4bc'
    tar -xzf "$deps_dir/downloads/glslang-15.1.0.tar.gz" -C "$deps_dir"
  fi
  "$cmake_bin" -S "$deps_dir/glslang-15.1.0" -B "$deps_dir/glslang-build" \
    -DCMAKE_BUILD_TYPE=Release -DENABLE_OPT=OFF -DENABLE_GLSLANG_BINARIES=ON \
    -DENABLE_SPVREMAPPER=OFF -DENABLE_HLSL=OFF -DBUILD_EXTERNAL=OFF
  "$cmake_bin" --build "$deps_dir/glslang-build" --parallel "${CG_BUILD_JOBS:-8}"
fi
printf 'Local dependencies are ready.\n'
