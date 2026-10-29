#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

namespace cg {

constexpr float pi = 3.14159265358979323846f;
constexpr float radians(float degrees) { return degrees * pi / 180.0f; }

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline Vec3 operator*(float s, Vec3 a) { return a * s; }
inline Vec3 operator/(Vec3 a, float s) { return a * (1.0f / s); }
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float length(Vec3 v) { return std::sqrt(dot(v, v)); }
inline Vec3 normalize(Vec3 v) {
    const float magnitude = length(v);
    if (!(magnitude > 0.0f) || !std::isfinite(magnitude))
        throw std::invalid_argument("Cannot normalize a zero or non-finite vector");
    return v / magnitude;
}

// UI directions can temporarily become zero while their components are edited.
// Keep them usable without changing the user-entered vector.
inline Vec3 safeNormalize(Vec3 v, Vec3 fallback = {0.0f, -1.0f, 0.0f}) {
    const float squaredLength = dot(v, v);
    if (!(squaredLength > 1e-12f) || !std::isfinite(squaredLength)) return normalize(fallback);
    return v / std::sqrt(squaredLength);
}

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;
};

// Column-major storage: these 16 floats can be copied directly to a GPU matrix.
// Vectors are columns, so A * B applies B first, then A.
struct Mat4 {
    std::array<float, 16> values{};

    float& operator()(int row, int column) { return values[column * 4 + row]; }
    float operator()(int row, int column) const { return values[column * 4 + row]; }
    float* data() { return values.data(); }
    const float* data() const { return values.data(); }

    static Mat4 identity() {
        Mat4 result;
        for (int i = 0; i != 4; ++i) result(i, i) = 1.0f;
        return result;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int row = 0; row != 4; ++row)
        for (int column = 0; column != 4; ++column)
            for (int k = 0; k != 4; ++k)
                result(row, column) += a(row, k) * b(k, column);
    return result;
}

inline Vec4 operator*(const Mat4& m, Vec4 v) {
    return {
        m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z + m(0, 3) * v.w,
        m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z + m(1, 3) * v.w,
        m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z + m(2, 3) * v.w,
        m(3, 0) * v.x + m(3, 1) * v.y + m(3, 2) * v.z + m(3, 3) * v.w,
    };
}

// Right-handed camera, looking along -Z. Clip-space depth is [0, 1].
// Set flipY=true when Vulkan uses a positive-height viewport.
inline Mat4 perspectiveRH_ZO(float verticalFov, float aspect, float nearPlane,
                             float farPlane, bool flipY = false) {
    if (!(verticalFov > 0.0f && verticalFov < pi && aspect > 0.0f &&
          nearPlane > 0.0f && farPlane > nearPlane))
        throw std::invalid_argument("Invalid perspective projection parameters");
    const float f = 1.0f / std::tan(verticalFov * 0.5f);
    Mat4 result;
    result(0, 0) = f / aspect;
    result(1, 1) = flipY ? -f : f;
    result(2, 2) = farPlane / (nearPlane - farPlane);
    result(2, 3) = farPlane * nearPlane / (nearPlane - farPlane);
    result(3, 2) = -1.0f;
    return result;
}

inline Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 forward = normalize(target - eye);
    const Vec3 right = normalize(cross(forward, up));
    const Vec3 cameraUp = cross(right, forward);
    Mat4 result = Mat4::identity();
    result(0, 0) = right.x; result(0, 1) = right.y; result(0, 2) = right.z;
    result(1, 0) = cameraUp.x; result(1, 1) = cameraUp.y; result(1, 2) = cameraUp.z;
    result(2, 0) = -forward.x; result(2, 1) = -forward.y; result(2, 2) = -forward.z;
    result(0, 3) = -dot(right, eye);
    result(1, 3) = -dot(cameraUp, eye);
    result(2, 3) = dot(forward, eye);
    return result;
}

} // namespace cg
