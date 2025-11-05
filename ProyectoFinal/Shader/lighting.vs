#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
layout (location=2) in vec2 aTex;

uniform mat4 model, view, projection;

out vec2 TexCoords;
out vec3 NormalWS;
out vec3 PosWS;

void main() {
    vec4 worldPos = model * vec4(aPos, 1.0);
    PosWS     = worldPos.xyz;
    NormalWS  = mat3(transpose(inverse(model))) * aNormal;
    TexCoords = aTex;
    gl_Position = projection * view * worldPos;
}
