#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirection;
    vec4 directionalColorIntensity;
    uvec4 lightCounts;
} scene;
struct PointLight { vec4 position; vec4 colorIntensity; };
struct SpotLight { vec4 position; vec4 directionInner; vec4 colorIntensity; vec4 outer; };
// Unsized arrays: light counts and allocations are controlled by the application.
layout(set = 0, binding = 1, std430) readonly buffer PointLights { PointLight points[]; };
layout(set = 0, binding = 2, std430) readonly buffer SpotLights { SpotLight spots[]; };
layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
    vec4 albedo;
    vec4 specularShininess;
} object;

vec3 safeNormalize(vec3 v) { return v * inversesqrt(max(dot(v, v), 1e-12)); }

vec3 blinnPhong(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo) {
    float diffuse = max(dot(N, L), 0.0);
    if (diffuse <= 0.0) return vec3(0.0); // No highlights from behind the surface.
    vec3 H = safeNormalize(L + V);
    float specular = pow(max(dot(N, H), 0.0), max(object.specularShininess.w, 1.0));
    return radiance * (albedo * diffuse + object.specularShininess.rgb * specular);
}

void main() {
    vec3 N = safeNormalize(worldNormal);
    vec3 V = safeNormalize(scene.cameraPosition.xyz - worldPosition);
    vec3 albedo = object.albedo.rgb;
    vec3 color = scene.ambient.rgb * scene.ambient.w * albedo;
    color += blinnPhong(N, V, safeNormalize(-scene.directionalDirection.xyz),
                       scene.directionalColorIntensity.rgb * scene.directionalColorIntensity.w, albedo);
    for (uint i = 0; i < scene.lightCounts.x; ++i) {
        vec3 delta = points[i].position.xyz - worldPosition;
        float distanceSquared = max(dot(delta, delta), 0.01);
        vec3 radiance = points[i].colorIntensity.rgb * points[i].colorIntensity.w / distanceSquared;
        color += blinnPhong(N, V, safeNormalize(delta), radiance, albedo);
    }
    for (uint i = 0; i < scene.lightCounts.y; ++i) {
        vec3 delta = spots[i].position.xyz - worldPosition;
        vec3 L = safeNormalize(delta);
        float cosine = dot(-L, spots[i].directionInner.xyz);
        float inner = spots[i].directionInner.w, outer = spots[i].outer.x;
        // Equal angles deliberately give a hard edge, without dividing by zero.
        float cone = inner > outer ? smoothstep(outer, inner, cosine) : step(outer, cosine);
        float distanceSquared = max(dot(delta, delta), 0.01);
        vec3 radiance = spots[i].colorIntensity.rgb * spots[i].colorIntensity.w * cone / distanceSquared;
        color += blinnPhong(N, V, L, radiance, albedo);
    }
    // Linear lighting is converted for the UNORM offscreen display texture.
    outColor = vec4(pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2)), 1.0);
}
