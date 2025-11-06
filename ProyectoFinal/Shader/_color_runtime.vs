#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
uniform mat4 model, view, projection;
out vec3 NormalWS;
void main(){
    mat3 nrmMat = mat3(transpose(inverse(model)));
    NormalWS = normalize(nrmMat * aNormal);
    gl_Position = projection * view * (model * vec4(aPos,1.0));
}
