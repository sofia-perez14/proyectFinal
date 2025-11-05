#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 NormalWS;
in vec3 PosWS;

uniform sampler2D texture_diffuse1;   // <- lo carga Mesh::Draw() en la unidad 0

// Luz direccional sencilla
uniform vec3 dirLight_direction = vec3(-0.2,-1.0,-0.3);
uniform vec3 dirLight_ambient   = vec3(0.6,0.6,0.6);
uniform vec3 dirLight_diffuse   = vec3(0.6,0.6,0.6);

void main() {
    vec3 base = texture(texture_diffuse1, TexCoords).rgb;
    // si el modelo no trae UV/tex, evita negro absoluto
    if (base == vec3(0.0)) base = vec3(0.7);

    vec3 N = normalize(NormalWS);
    vec3 L = normalize(-dirLight_direction);
    float ndl = max(dot(N, L), 0.0);

    vec3 color = base * (dirLight_ambient + dirLight_diffuse * ndl);
    FragColor = vec4(color, 1.0);
}
