#include "../src/cylinder.hpp"
#include "../src/scene.hpp"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float actual, float expected, float tolerance = 1e-5f) {
    return std::abs(actual - expected) <= tolerance;
}

bool near(cg::Vec3 a, cg::Vec3 b, float tolerance = 1e-5f) {
    return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

bool near(const cg::Mat4& a, const cg::Mat4& b, float tolerance = 1e-5f) {
    for (std::size_t i = 0; i < a.values.size(); ++i)
        if (!near(a.values[i], b.values[i], tolerance)) return false;
    return true;
}

cg::Vec3 position(const cg::Vertex& vertex) {
    return {vertex.position[0], vertex.position[1], vertex.position[2]};
}

cg::Vec3 xyz(cg::Vec4 v) { return {v.x, v.y, v.z}; }

cg::Vec3 project(const cg::Mat4& matrix, cg::Vec3 p) {
    const cg::Vec4 clip = matrix * cg::Vec4{p.x, p.y, p.z, 1.0f};
    return xyz(clip) / clip.w;
}

template<class Function>
void requiresInvalidArgument(Function operation, const std::string& message) {
    bool threw = false;
    try { operation(); }
    catch (const std::invalid_argument&) { threw = true; }
    require(threw, message);
}

void cylinderTest() {
    const auto mesh = cg::makeCylinder();
    require(mesh.vertices.size() == 202, "Cylinder must split side and cap vertices for hard normal edges");
    require(mesh.indices.size() == 600, "Cylinder must have 200 triangles");

    // Check the unique positions and geometric topology of the closed surface.
    using PositionKey = std::array<long, 3>;
    std::map<PositionKey, std::size_t> welded;
    std::vector<std::size_t> vertexIds;
    std::set<PositionKey> bottomRim, topRim;
    std::map<PositionKey, std::vector<cg::Vec3>> normalsAtPosition;
    for (const auto& vertex : mesh.vertices) {
        const cg::Vec3 p = position(vertex);
        const PositionKey key{std::lround(p.x * 1000000.0f),
                              std::lround(p.y * 1000000.0f),
                              std::lround(p.z * 1000000.0f)};
        const auto inserted = welded.emplace(key, welded.size());
        vertexIds.push_back(inserted.first->second);
        const cg::Vec3 normal{vertex.normal[0], vertex.normal[1], vertex.normal[2]};
        require(near(cg::length(normal), 1.0f), "Procedural normals must have unit length");
        normalsAtPosition[key].push_back(normal);
        const float radialDistance = std::sqrt(p.x * p.x + p.z * p.z);
        require(near(radialDistance, 1.0f) || near(radialDistance, 0.0f), "Vertex off cylinder boundary");
        require(near(std::abs(p.y), 1.0f), "Unexpected cylinder height");
        if (near(radialDistance, 1.0f)) {
            if (p.y < 0.0f) bottomRim.insert(key);
            else topRim.insert(key);
        }

    }
    require(welded.size() == 102, "Unexpected number of geometric vertices");
    for (const auto& [key, normals] : normalsAtPosition) {
        const bool isCenter = key[0] == 0 && key[2] == 0;
        require(normals.size() == (isCenter ? 1u : 2u), "Only the rim needs duplicate normal vertices");
        if (!isCenter) {
            require(near(cg::dot(normals[0], normals[1]), 0.0f), "Side and cap normals must meet at a hard right angle");
            const cg::Vec3 radial{static_cast<float>(key[0]) / 1000000.0f, 0,
                                  static_cast<float>(key[2]) / 1000000.0f};
            require(near(normals[0], radial), "Side normals must point radially outwards");
        }
        const cg::Vec3 capNormal{0, key[1] < 0 ? -1.0f : 1.0f, 0};
        require(near(normals.back(), capNormal), "Cap normals must point along the cylinder axis");
    }
    require(bottomRim.size() == 50 && topRim.size() == 50, "Each base needs 50 boundary points");

    struct EdgeUse { int count = 0; int orientation = 0; };
    std::map<std::pair<std::size_t, std::size_t>, EdgeUse> edges;
    double signedVolume = 0.0;
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
        const auto ia = mesh.indices[triangle], ib = mesh.indices[triangle + 1], ic = mesh.indices[triangle + 2];
        require(ia < mesh.vertices.size() && ib < mesh.vertices.size() && ic < mesh.vertices.size(),
                "Triangle index out of range");
        const cg::Vec3 a = position(mesh.vertices[ia]);
        const cg::Vec3 b = position(mesh.vertices[ib]);
        const cg::Vec3 c = position(mesh.vertices[ic]);
        const cg::Vec3 faceNormal = cg::cross(b - a, c - a);
        const cg::Vec3 centroid = (a + b + c) / 3.0f;
        require(cg::length(faceNormal) > 1e-5f, "Degenerate triangle");
        require(cg::dot(faceNormal, centroid) > 0.0f, "Triangle winding must face outward");
        for (auto index : {ia, ib, ic}) {
            const auto& v = mesh.vertices[index];
            require(cg::dot(faceNormal, {v.normal[0], v.normal[1], v.normal[2]}) > 0.0f,
                    "Each vertex normal must face the same hemisphere as its triangle");
        }
        signedVolume += static_cast<double>(cg::dot(a, cg::cross(b, c))) / 6.0;
        const std::array<std::size_t, 3> ids{vertexIds[ia], vertexIds[ib], vertexIds[ic]};
        for (int edge = 0; edge != 3; ++edge) {
            const auto from = ids[edge], to = ids[(edge + 1) % 3];
            auto& use = edges[{std::min(from, to), std::max(from, to)}];
            ++use.count;
            use.orientation += from < to ? 1 : -1;
        }
    }
    for (const auto& entry : edges)
        require(entry.second.count == 2 && entry.second.orientation == 0,
                "Each geometric edge must be shared by two oppositely oriented triangles");
    require(static_cast<long>(welded.size()) - static_cast<long>(edges.size()) +
            static_cast<long>(mesh.indices.size() / 3) == 2, "Cylinder topology must have Euler characteristic 2");
    const double expectedVolume = 50.0 * std::sin(2.0 * static_cast<double>(cg::pi) / 50.0);
    require(std::abs(signedVolume - expectedVolume) < 1e-5, "Cylinder volume is incorrect");

    const auto small = cg::makeCylinder(3, 2.0f, 6.0f);
    require(small.indices.size() == 36 && small.vertices.size() == 14, "Generalized segment count failed");
    requiresInvalidArgument([] { cg::makeCylinder(2); }, "Reject fewer than three segments");
    requiresInvalidArgument([] { cg::makeCylinder(50, 0.0f); }, "Reject zero radius");
}

void matrixTest() {
    const cg::Vec3 eye{3, 4, 5};
    const auto view = cg::lookAt(eye, {0, 0, 0}, {0, 1, 0});
    require(near(project(view, eye), {0, 0, 0}), "View must map camera to origin");
    require(near(project(view, {0, 0, 0}), {0, 0, -cg::length(eye)}), "View must face along negative Z");
    requiresInvalidArgument([] { cg::lookAt({0, 0, 0}, {0, 0, 0}, {0, 1, 0}); }, "Reject degenerate camera");

    const float nearPlane = 0.1f, farPlane = 100.0f;
    const auto perspective = cg::perspectiveRH_ZO(cg::pi * 0.5f, 2.0f, nearPlane, farPlane);
    require(near(project(perspective, {0, 0, -nearPlane}).z, 0), "Perspective near must map to zero");
    require(near(project(perspective, {0, 0, -farPlane}).z, 1), "Perspective far must map to one");
    require(near(project(perspective, {1, 0, -2}).x * 2.0f,
                 project(perspective, {1, 0, -1}).x), "Perspective must shrink distant objects");
    require(near(project(perspective, {2, 1, -1}), {1, 1, project(perspective, {0, 0, -1}).z}),
            "Perspective FOV or aspect ratio incorrect");
    const auto vulkanPerspective = cg::perspectiveRH_ZO(cg::pi * 0.5f, 2, nearPlane, farPlane, true);
    require(near(project(vulkanPerspective, {0, 1, -1}).y, -1), "Vulkan perspective Y flip");

    requiresInvalidArgument([] { cg::perspectiveRH_ZO(1, 0, 0.1f, 10); }, "Reject zero aspect");
}

void cameraTest() {
    cg::Camera camera;
    require(near(camera.forward(), cg::normalize(-camera.position), 1e-4f),
            "The initial camera must face the cylinder");
    require(near(project(camera.view(), camera.position), {0, 0, 0}),
            "Camera view must map its position to the origin");
    require(near(project(camera.view(), camera.position + camera.forward()), {0, 0, -1}),
            "Camera view must look along local negative Z");

    camera.yawDegrees = -90;
    camera.pitchDegrees = 0;
    camera.rollDegrees = 0;
    require(near(camera.forward(), {0, 0, -1}) && near(camera.right(), {1, 0, 0}) &&
            near(camera.up(), {0, 1, 0}), "Camera yaw basis");
    camera.yawDegrees = 0;
    require(near(camera.forward(), {1, 0, 0}), "Changing yaw must rotate the view");
    camera.yawDegrees = -90;
    camera.pitchDegrees = 30;
    require(near(camera.forward().y, 0.5f), "Changing pitch must raise the view");
    camera.pitchDegrees = 0;
    camera.rollDegrees = 90;
    require(near(camera.right(), {0, 1, 0}) && near(camera.up(), {-1, 0, 0}),
            "Camera roll must rotate its right and up axes");

    for (float pitch : {-90.0f, -89.0f, 0.0f, 89.0f, 90.0f}) {
        camera.pitchDegrees = pitch;
        camera.yawDegrees = 43;
        camera.rollDegrees = 23;
        const auto forward = camera.forward(), right = camera.right(), up = camera.up();
        require(near(cg::length(forward), 1) && near(cg::length(right), 1) && near(cg::length(up), 1),
                "Camera basis must stay unit length, including vertical views");
        require(near(cg::dot(forward, right), 0) && near(cg::dot(forward, up), 0) &&
                near(cg::dot(right, up), 0), "Camera basis must be orthogonal");
        require(near(cg::cross(right, forward), up), "Camera basis handedness");
        require(near(project(camera.view(), camera.position + forward * 3.0f), {0, 0, -3}),
                "Rolled or vertical camera must produce a valid view");
    }
    require(near(cg::safeNormalize({0, 0, 0}), {0, -1, 0}), "Zero light direction must use a safe fallback");
    require(near(cg::safeNormalize({2, 0, 0}), {1, 0, 0}), "Light directions must normalize");
}

void sceneTest() {
    const cg::Scene scene;
    require(near(scene.model.transform, cg::Mat4::identity()),
            "The cylinder model must start at the origin with an identity transform");
    require(scene.model.material.shininess >= 1.0f, "The material must have a valid specular exponent");
    require(near(cg::Mat4::identity() * scene.camera.view(), scene.camera.view()),
            "Identity multiplication must preserve the view matrix");
}

} // namespace

int main() {
    try {
        cylinderTest();
        matrixTest();
        cameraTest();
        sceneTest();
        std::cout << "PASS: cylinder topology and hard-edge normals; view and perspective matrices; camera basis; model material\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
