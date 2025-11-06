#version 330 core
out vec4 FragColor;
in vec3 NormalWS;
uniform vec3 uColor;
uniform vec3 dirLight_direction = vec3(-0.2,-1.0,-0.3);
uniform vec3 dirLight_ambient   = vec3(0.45,0.45,0.45);
uniform vec3 dirLight_diffuse   = vec3(0.7,0.7,0.7);
void main(){
    vec3 n = normalize(NormalWS);
    float ndl = max(dot(n, normalize(-dirLight_direction)), 0.0);
    vec3 color = uColor*(dirLight_ambient + dirLight_diffuse*ndl);
    FragColor = vec4(color,1.0);
}
