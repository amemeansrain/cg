#pragma once

#include "math.hpp"
#include <cstddef>
#include <vector>

namespace cg {

struct Object {
    Vec3 position{0.0f, 0.0f, 0.0f};
    Vec3 rotation{0.0f, 0.0f, 0.0f}; // Radians, applied X, then Y, then Z.
    Vec3 scale{1.0f, 1.0f, 1.0f};
    Vec3 tint{1.0f, 1.0f, 1.0f};
};

struct Animation {
    bool playing = false;
    double time = 0.0;
    float speed = 1.0f;
    float pathRadius = 1.0f;
    float pathHeight = 0.35f;
    float rotationSpeed = 0.7f;
};

// A closed figure-eight in XZ, with an additional vertical oscillation.
inline Vec3 animationOffset(const Animation& animation) {
    const double t = animation.time;
    return {
        animation.pathRadius * static_cast<float>(std::sin(t)),
        animation.pathHeight * static_cast<float>(std::sin(3.0 * t)),
        0.5f * animation.pathRadius * static_cast<float>(std::sin(2.0 * t)),
    };
}

inline Mat4 modelMatrix(const Object& object, const Animation& animation) {
    Vec3 position = object.position;
    Vec3 rotation = object.rotation;
    position = position + animationOffset(animation);
    const float angle = static_cast<float>(animation.time * animation.rotationSpeed);
    rotation.x += 0.5f * angle;
    rotation.y += angle;
    rotation.z += 0.25f * angle;
    return translate(position) * rotateZ(rotation.z) * rotateY(rotation.y) *
           rotateX(rotation.x) * scale(object.scale);
}

struct Scene {
    std::vector<Object> objects{Object{}};
    Animation animation;

    void update(double dt) {
        if (animation.playing && dt > 0.0 && std::isfinite(dt))
            animation.time += dt * static_cast<double>(animation.speed);
    }

    Mat4 modelMatrix(std::size_t index) const {
        return cg::modelMatrix(objects.at(index), animation);
    }
};

} // namespace cg
