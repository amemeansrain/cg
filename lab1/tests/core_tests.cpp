#include "../src/cylinder.hpp"
#include "../src/scene.hpp"
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
    require(mesh.vertices.size() == 102, "Cylinder must have 100 rim vertices and two cap centers");
    require(mesh.indices.size() == 600, "Cylinder must have 200 triangles");

    // Check the unique positions and geometric topology of the closed surface.
    using PositionKey = std::array<long, 3>;
    std::map<PositionKey, std::size_t> welded;
    std::vector<std::size_t> vertexIds;
    std::set<PositionKey> bottomRim, topRim;
    for (const auto& vertex : mesh.vertices) {
        const cg::Vec3 p = position(vertex);
        const PositionKey key{std::lround(p.x * 1000000.0f),
                              std::lround(p.y * 1000000.0f),
                              std::lround(p.z * 1000000.0f)};
        const auto inserted = welded.emplace(key, welded.size());
        vertexIds.push_back(inserted.first->second);
        const float radialDistance = std::sqrt(p.x * p.x + p.z * p.z);
        require(near(radialDistance, 1.0f) || near(radialDistance, 0.0f), "Vertex off cylinder boundary");
        require(near(std::abs(p.y), 1.0f), "Unexpected cylinder height");
        if (near(radialDistance, 1.0f)) {
            if (p.y < 0.0f) bottomRim.insert(key);
            else topRim.insert(key);
        }
        for (int component = 0; component != 3; ++component) {
            const float color = vertex.color[component];
            require(std::isfinite(color) && color >= 0.0f && color <= 1.0f, "Color outside [0,1]");
            require(near(color, 0.5f + 0.5f * vertex.position[component]), "Color must derive from position");
        }
    }
    require(welded.size() == 102, "Unexpected number of geometric vertices");
    require(welded.size() == mesh.vertices.size(), "Rim vertices must be shared without duplicate positions");
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
    require(small.indices.size() == 36 && small.vertices.size() == 8, "Generalized segment count failed");
    requiresInvalidArgument([] { cg::makeCylinder(2); }, "Reject fewer than three segments");
    requiresInvalidArgument([] { cg::makeCylinder(50, 0.0f); }, "Reject zero radius");
}

void matrixTest() {
    const cg::Mat4 transform = cg::translate({3.0f, 4.0f, 5.0f}) *
                               cg::rotateZ(cg::pi * 0.5f) * cg::scale({2.0f, 3.0f, 4.0f});
    const auto point = transform * cg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
    require(near(xyz(point), {3.0f, 6.0f, 5.0f}) && near(point.w, 1.0f), "Wrong transform order");
    require(near(xyz(cg::rotateX(cg::pi * 0.5f) * cg::Vec4{0, 1, 0, 0}), {0, 0, 1}), "X rotation");
    require(near(xyz(cg::rotateY(cg::pi * 0.5f) * cg::Vec4{0, 0, 1, 0}), {1, 0, 0}), "Y rotation");
    require(near(cg::Mat4::identity() * transform, transform), "Identity multiplication");
    require(near(transform.data()[12], 3.0f) && near(transform.data()[14], 5.0f), "Column-major matrix layout");

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

    const auto ortho = cg::orthoRH_ZO(-2, 4, -1, 3, nearPlane, farPlane);
    require(near(project(ortho, {-2, -1, -nearPlane}), {-1, -1, 0}), "Ortho near corner");
    require(near(project(ortho, {4, 3, -farPlane}), {1, 1, 1}), "Ortho far corner");
    require(near(project(ortho, {2, 1, -2}).x, project(ortho, {2, 1, -20}).x), "Ortho must preserve apparent size");
    const auto flippedOrtho = cg::orthoRH_ZO(-2, 4, -1, 3, nearPlane, farPlane, true);
    require(near(project(flippedOrtho, {4, 3, -farPlane}), {1, -1, 1}), "Asymmetric ortho Y flip");
    requiresInvalidArgument([] { cg::perspectiveRH_ZO(1, 0, 0.1f, 10); }, "Reject zero aspect");
    requiresInvalidArgument([] { cg::orthoRH_ZO(0, 0, -1, 1, 0, 10); }, "Reject zero ortho width");
}

void sceneTest() {
    cg::Scene scene;
    const auto initial = scene.modelMatrix(0);
    require(!scene.animation.playing && scene.animation.time == 0.0, "Scene must start paused");
    scene.update(1.0);
    require(near(scene.modelMatrix(0), initial), "Initial pause must preserve the manual pose");
    scene.animation.playing = true;
    scene.update(0.25);
    require(std::abs(scene.animation.time - 0.25) < 1e-10, "Animation time advance");
    require(!near(scene.modelMatrix(0), initial), "Animation must move the cylinder");
    scene.animation.playing = false;
    const auto paused = scene.modelMatrix(0);
    scene.update(10.0);
    require(near(scene.modelMatrix(0), paused), "Paused animation must not drift");
    scene.animation.speed = 2.0f;
    require(near(scene.modelMatrix(0), paused), "Changing speed must not jump to another phase");
    scene.animation.playing = true;
    scene.update(0.25);
    require(std::abs(scene.animation.time - 0.75) < 1e-10, "Speed must scale elapsed time");
    scene.update(-1.0);
    require(std::abs(scene.animation.time - 0.75) < 1e-10, "Negative dt must be ignored");
    scene.animation.speed = 0.0f;
    const auto stopped = scene.modelMatrix(0);
    scene.update(1.0);
    require(near(scene.modelMatrix(0), stopped), "Zero speed must freeze the current phase");

    cg::Scene stepped, singleStep;
    stepped.animation.playing = singleStep.animation.playing = true;
    for (int step = 0; step < 100; ++step) stepped.update(0.01);
    singleStep.update(1.0);
    require(near(stepped.modelMatrix(0), singleStep.modelMatrix(0)), "Animation must be frame-rate independent");

    cg::Animation animation;
    animation.time = cg::pi * 0.5;
    animation.pathRadius = 3.0f;
    animation.pathHeight = 0.75f;
    require(near(cg::animationOffset(animation), {3, -0.75f, 0}), "Trajectory parameters must control amplitude");
    animation.time = 0;
    const auto start = cg::animationOffset(animation);
    animation.time = 2.0 * cg::pi;
    require(near(cg::animationOffset(animation), start), "Figure-eight trajectory must close");
}

} // namespace

int main() {
    try {
        cylinderTest();
        matrixTest();
        sceneTest();
        std::cout << "PASS: cylinder topology and colors; transforms and projections; animation controls\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
