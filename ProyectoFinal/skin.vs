#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTex;
layout (location = 5) in ivec4 aBoneIDs;
layout (location = 6) in vec4  aWeights;

uniform mat4 model, view, projection;
uniform mat4 bones[100];

out vec2 TexCoords;
out vec3 NormalWS;
out vec3 PosWS;

void main()
{
    // Skinning matrix
    mat4 skin =
        aWeights.x * bones[aBoneIDs.x] +
        aWeights.y * bones[aBoneIDs.y] +
        aWeights.z * bones[aBoneIDs.z] +
        aWeights.w * bones[aBoneIDs.w];

    vec4 localPos = skin * vec4(aPos, 1.0);
    vec4 worldPos = model * localPos;

    PosWS    = worldPos.xyz;
    // Normal transform: usa skin y model
    mat3 nrmMat = mat3(transpose(inverse(model))) * mat3(skin);
    NormalWS = normalize(nrmMat * aNormal);

    gl_Position = projection * view * worldPos;
    TexCoords = aTex;
}
