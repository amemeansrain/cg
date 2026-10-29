#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirection;
    vec4 directionalColorIntensity;
    uvec4 lightCounts;
} scene;
layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
    vec4 albedo;
    vec4 specularShininess;
} object;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;

void main() {
    vec4 world = object.model * vec4(inPosition, 1.0);
    gl_Position = scene.viewProjection * world;
    worldPosition = world.xyz;
    // Unlike the model matrix, this preserves perpendicularity after nonuniform scale.
    worldNormal = transpose(inverse(mat3(object.model))) * inNormal;
}
