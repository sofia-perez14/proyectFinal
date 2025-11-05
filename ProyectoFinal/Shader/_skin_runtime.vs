#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
layout (location=2) in vec2 aTex;
layout (location=5) in ivec4 aBoneIDs;
layout (location=6) in vec4  aWeights;
uniform mat4 model, view, projection;
uniform mat4 bones[100];
out vec2 TexCoords;
out vec3 NormalWS;
out vec3 PosWS;
void main(){
    float wsum = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
    mat4 skin = mat4(1.0);
    if(wsum>0.0001){
        skin = aWeights.x*bones[aBoneIDs.x] +
               aWeights.y*bones[aBoneIDs.y] +
               aWeights.z*bones[aBoneIDs.z] +
               aWeights.w*bones[aBoneIDs.w];
    }
    vec4 worldPos = model * (skin * vec4(aPos,1.0));
    PosWS = worldPos.xyz;
    mat3 nrmMat = mat3(transpose(inverse(model))) * mat3(skin);
    NormalWS = normalize(nrmMat * aNormal);
    TexCoords = aTex;
    gl_Position = projection * view * worldPos;
}
