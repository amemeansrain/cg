#pragma once

#include "math.hpp"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace cg {

struct Vertex {
    float position[3];
    float color[3];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// The cylinder's axis is Y. Each cap shares its boundary vertices with the side.
// There are `segments` points on each rim and one center for each cap.
inline Mesh makeCylinder(std::uint32_t segments = 50, float radius = 1.0f,
                         float height = 2.0f) {
    if (segments < 3 || segments > 1000000 || !(radius > 0.0f) ||
        !(height > 0.0f) || !std::isfinite(radius) || !std::isfinite(height))
        throw std::invalid_argument("Invalid cylinder dimensions or segment count");

    Mesh mesh;
    mesh.vertices.reserve(2 * segments + 2);
    mesh.indices.reserve(12 * segments);
    const float halfHeight = height * 0.5f;
    const auto vertex = [radius, halfHeight](Vec3 p) {
        // Map local coordinates from [-extent, extent] into RGB [0, 1].
        return Vertex{{p.x, p.y, p.z}, {
            std::clamp(0.5f + 0.5f * p.x / radius, 0.0f, 1.0f),
            std::clamp(0.5f + 0.5f * p.y / halfHeight, 0.0f, 1.0f),
            std::clamp(0.5f + 0.5f * p.z / radius, 0.0f, 1.0f),
        }};
    };

    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        const float x = radius * std::cos(angle), z = radius * std::sin(angle);
        mesh.vertices.push_back(vertex({x, -halfHeight, z}));
        mesh.vertices.push_back(vertex({x, +halfHeight, z}));
    }
    for (std::uint32_t i = 0; i < segments; ++i) {
        const std::uint32_t next = (i + 1) % segments;
        // Counterclockwise when the surface is viewed from outside.
        mesh.indices.insert(mesh.indices.end(), {
            2 * i, 2 * i + 1, 2 * next + 1,
            2 * i, 2 * next + 1, 2 * next,
        });
    }

    for (std::uint32_t cap = 0; cap < 2; ++cap) {
        const float y = cap == 0 ? -halfHeight : halfHeight;
        const auto center = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(vertex({0.0f, y, 0.0f}));
        for (std::uint32_t i = 0; i < segments; ++i) {
            const std::uint32_t current = 2 * i + cap;
            const std::uint32_t next = 2 * ((i + 1) % segments) + cap;
            if (cap == 0) mesh.indices.insert(mesh.indices.end(), {center, current, next});
            else mesh.indices.insert(mesh.indices.end(), {center, next, current});
        }
    }
    return mesh;
}

} // namespace cg
