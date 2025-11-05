#version 330 core
out vec4 FragColor;
in vec2 TexCoords;
in vec3 NormalWS;
uniform sampler2D texture_diffuse1;
uniform vec3 dirLight_direction = vec3(-0.2,-1.0,-0.3);
uniform vec3 dirLight_ambient   = vec3(0.6,0.6,0.6);
uniform vec3 dirLight_diffuse   = vec3(0.6,0.6,0.6);
void main(){
    vec3 base = texture(texture_diffuse1, TexCoords).rgb;
    if(base == vec3(0.0)) base = vec3(0.6);
    vec3 n = normalize(NormalWS);
    float ndl = max(dot(n, normalize(-dirLight_direction)), 0.0);
    vec3 color = base*(dirLight_ambient + dirLight_diffuse*ndl);
    FragColor = vec4(color,1.0);
}
