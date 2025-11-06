#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uTiling = vec2(1.0, 1.0);
void main(){
    FragColor = texture(uTex, vUV * uTiling);
}
