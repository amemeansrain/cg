#pragma once

#include "math.hpp"
#include <vector>

namespace cg {

struct Material {
    Vec3 albedo{0.65f, 0.45f, 0.25f};
    Vec3 specular{1.0f, 1.0f, 1.0f};
    float shininess = 64.0f;
};

// Euler angles are exposed in degrees, while all math helpers take radians.
// Yaw 0 faces +X, yaw -90 faces -Z. Positive pitch looks upward.
struct Camera {
    Vec3 position{6.0f, 4.0f, 8.0f};
    float yawDegrees = -126.87f;
    float pitchDegrees = -21.8f;
    float rollDegrees = 0.0f;
    float fovDegrees = 45.0f;

    Vec3 forward() const {
        const float yaw = radians(yawDegrees), pitch = radians(pitchDegrees);
        return safeNormalize({std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                              std::sin(yaw) * std::cos(pitch)}, {0, 0, -1});
    }

    Vec3 right() const {
        const float yaw = radians(yawDegrees), roll = radians(rollDegrees);
        const Vec3 horizontalRight{-std::sin(yaw), 0, std::cos(yaw)};
        const Vec3 baseUp = cross(horizontalRight, forward());
        return safeNormalize(horizontalRight * std::cos(roll) + baseUp * std::sin(roll), {1, 0, 0});
    }

    Vec3 up() const { return safeNormalize(cross(right(), forward()), {0, 1, 0}); }
    Mat4 view() const { return lookAt(position, position + forward(), up()); }
};

struct DirectionalLight {
    // Direction in which rays travel, from the light towards the scene.
    Vec3 direction{-0.5f, -1.0f, -0.3f};
    Vec3 color{1.0f, 0.94f, 0.83f};
    float intensity = 0.8f;
};

struct PointLight {
    Vec3 position{-2.0f, 2.0f, 2.0f};
    Vec3 color{0.3f, 0.65f, 1.0f};
    float intensity = 12.0f;
};

struct SpotLight {
    Vec3 position{2.0f, 3.0f, 1.0f};
    Vec3 direction{-2.0f, -3.0f, -1.0f};
    Vec3 color{1.0f, 0.65f, 0.3f};
    float intensity = 24.0f;
    // Half-angles measured from the cone axis, in degrees.
    float innerAngleDegrees = 18.0f;
    float outerAngleDegrees = 28.0f;
};

struct Model {
    Mat4 transform = Mat4::identity();
    Material material;
};

struct Scene {
    Model model;
    Camera camera;
    Vec3 ambientColor{1.0f, 1.0f, 1.0f};
    float ambientIntensity = 0.08f;
    DirectionalLight directional;
    std::vector<PointLight> pointLights{PointLight{}};
    std::vector<SpotLight> spotLights{SpotLight{}};
    bool showLightMarkers = true;
};

} // namespace cg
