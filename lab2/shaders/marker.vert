#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirection;
    vec4 directionalColorIntensity;
    uvec4 lightCounts;
} scene;
layout(location = 0) out vec3 color;
void main() {
    gl_Position = scene.viewProjection * vec4(inPosition, 1.0);
    color = inColor;
}
