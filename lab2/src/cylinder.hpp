#pragma once

#include "math.hpp"
#include <cstdint>
#include <vector>

namespace cg {

struct Vertex {
    float position[3];
    float normal[3];
};

struct Mesh {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

// The cylinder's axis is Y. Caps have their own boundary vertices so their
// vertical normals do not blend with the radial normals of the curved side.
// There are still `segments` geometric points on each rim, plus two centers.
inline Mesh makeCylinder(std::uint32_t segments = 50, float radius = 1.0f,
                         float height = 2.0f) {
    if (segments < 3 || segments > 1000000 || !(radius > 0.0f) ||
        !(height > 0.0f) || !std::isfinite(radius) || !std::isfinite(height))
        throw std::invalid_argument("Invalid cylinder dimensions or segment count");

    Mesh mesh;
    mesh.vertices.reserve(4 * segments + 2);
    mesh.indices.reserve(12 * segments);
    const float halfHeight = height * 0.5f;
    const auto vertex = [](Vec3 p, Vec3 normal) {
        return Vertex{{p.x, p.y, p.z}, {normal.x, normal.y, normal.z}};
    };

    for (std::uint32_t i = 0; i < segments; ++i) {
        const float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(segments);
        const float x = radius * std::cos(angle), z = radius * std::sin(angle);
        const Vec3 normal{x / radius, 0.0f, z / radius};
        mesh.vertices.push_back(vertex({x, -halfHeight, z}, normal));
        mesh.vertices.push_back(vertex({x, +halfHeight, z}, normal));
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
        const Vec3 normal{0.0f, cap == 0 ? -1.0f : 1.0f, 0.0f};
        const auto rim = static_cast<std::uint32_t>(mesh.vertices.size());
        // Copy positions exactly from the side to keep the geometric seam closed.
        for (std::uint32_t i = 0; i < segments; ++i) {
            const auto& side = mesh.vertices[2 * i + cap];
            mesh.vertices.push_back(vertex({side.position[0], y, side.position[2]}, normal));
        }
        const auto center = static_cast<std::uint32_t>(mesh.vertices.size());
        mesh.vertices.push_back(vertex({0.0f, y, 0.0f}, normal));
        for (std::uint32_t i = 0; i < segments; ++i) {
            const std::uint32_t current = rim + i;
            const std::uint32_t next = rim + ((i + 1) % segments);
            if (cap == 0) mesh.indices.insert(mesh.indices.end(), {center, current, next});
            else mesh.indices.insert(mesh.indices.end(), {center, next, current});
        }
    }
    return mesh;
}

} // namespace cg
