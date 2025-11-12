#version 330 core
out vec4 FragColor;

in vec2 TexCoords;
in vec3 NormalWS;
in vec3 PosWS;

uniform sampler2D texture_diffuse1;

// Luz direccional sencilla
uniform vec3 dirLight_direction = vec3(-0.2,-1.0,-0.3);
uniform vec3 dirLight_ambient   = vec3(0.6,0.6,0.6);
uniform vec3 dirLight_diffuse   = vec3(0.6,0.6,0.6);

// Emisión para objetos que brillan
uniform vec3 emissiveColor = vec3(0.0, 0.0, 0.0);
uniform float emissiveStrength = 0.0;

// Spotlights
struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
    
    float cutOff;
    float outerCutOff;
    
    float constant;
    float linear;
    float quadratic;
};

#define NR_SPOT_LIGHTS 3
uniform SpotLight spotLights[NR_SPOT_LIGHTS];

vec3 CalcSpotLight(SpotLight light, vec3 normal, vec3 fragPos, vec3 baseColor)
{
    vec3 lightDir = normalize(light.position - fragPos);
    
    // Spotlight intensity
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.cutOff - light.outerCutOff;
    float intensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    
    if (intensity <= 0.0) return vec3(0.0);
    
    // Diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Attenuation
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (light.constant + light.linear * distance + light.quadratic * (distance * distance));
    
    // Combine results
    vec3 ambient = light.ambient * baseColor;
    vec3 diffuse = light.diffuse * diff * baseColor;
    
    ambient *= attenuation * intensity;
    diffuse *= attenuation * intensity;
    
    return (ambient + diffuse);
}

void main() {
    vec3 base = texture(texture_diffuse1, TexCoords).rgb;
    // si el modelo no trae UV/tex, evita negro absoluto
    if (base == vec3(0.0)) base = vec3(0.7);

    vec3 N = normalize(NormalWS);
    vec3 L = normalize(-dirLight_direction);
    float ndl = max(dot(N, L), 0.0);

    vec3 color = base * (dirLight_ambient + dirLight_diffuse * ndl);
    
    // Agregar spotlights
    for(int i = 0; i < NR_SPOT_LIGHTS; i++) {
        color += CalcSpotLight(spotLights[i], N, PosWS, base);
    }
    
    // Agregar emisión (luz propia del objeto)
    vec3 emission = emissiveColor * emissiveStrength;
    color += emission;
    
    FragColor = vec4(color, 1.0);
}
