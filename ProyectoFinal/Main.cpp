// Proyecto final. Galería de videojuegos
// Integrantes: Perez Ortiz Sofia, Sanchez Zamora Jesus, Reynoso Ortega Francisco Javier
// Fecha de entrega: 19 de noviembre de 2025

#include <iostream>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "stb_image.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "SOIL2/SOIL2.h"
#include "Shader.h"
#include "Camera.h"
#include "Model.h"

// ====== SHADERS EMBEBIDOS ======
static const char* SKIN_VS_SRC = R"(#version 330 core
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
)";

static const char* TEX_FRAG_SRC = R"(#version 330 core
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
)";

// === Shader de color (sólido) ===
static const char* COLOR_VS_SRC = R"(#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
uniform mat4 model, view, projection;
out vec3 NormalWS;
void main(){
    mat3 nrmMat = mat3(transpose(inverse(model)));
    NormalWS = normalize(nrmMat * aNormal);
    gl_Position = projection * view * (model * vec4(aPos,1.0));
}
)";

static const char* COLOR_FRAG_SRC = R"(#version 330 core
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
)";

// === Shader QUAD texturizado (tiling) ===
static const char* QUAD_VS_SRC = R"(#version 330 core
layout (location=0) in vec3 aPos;
layout (location=1) in vec3 aNormal;
layout (location=2) in vec2 aTex;
uniform mat4 model, view, projection;
out vec2 vUV;
void main(){
    vUV = aTex;
    gl_Position = projection * view * (model * vec4(aPos,1.0));
}
)";

static const char* QUAD_FRAG_SRC = R"(#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uTiling = vec2(1.0, 1.0);
void main(){
    FragColor = texture(uTex, vUV * uTiling);
}
)";

// === Skybox shaders ===
static const char* SKYBOX_VS_SRC = R"(#version 330 core
layout (location=0) in vec3 aPos;
out vec3 TexDir;
uniform mat4 view;
uniform mat4 projection;
void main(){
    mat4 viewNoT = mat4(mat3(view));
    vec4 pos = projection * viewNoT * vec4(aPos,1.0);
    gl_Position = pos.xyww;
    TexDir = aPos;
}
)";

static const char* SKYBOX_FRAG_SRC = R"(#version 330 core
in vec3 TexDir;
out vec4 FragColor;
uniform samplerCube skybox;
void main(){
    FragColor = texture(skybox, TexDir);
}
)";

static bool WriteTextFile(const char* path, const char* src) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, path, "wb");
#else
    f = fopen(path, "wb");
#endif
    if (!f) return false;
    fwrite(src, 1, strlen(src), f);
    fclose(f);
    return true;
}

// prototipos
void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void MouseCallback(GLFWwindow* window, double xPos, double yPos);
void DoMovement();
// Estructura de keyframe
struct Keyframe {
    float time;
    glm::vec3 position;
    float rotation;
};

// Interpolación lineal entre keyframes
glm::vec3 interpolatePosition(const std::vector<Keyframe>& keys, float t) {
    if (keys.empty()) return glm::vec3(0.0f);
    if (t <= keys[0].time) return keys[0].position;
    if (t >= keys.back().time) return keys.back().position;

    for (size_t i = 0; i < keys.size() - 1; i++) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            float alpha = (t - keys[i].time) / (keys[i + 1].time - keys[i].time);
            return glm::mix(keys[i].position, keys[i + 1].position, alpha);
        }
    }
    return keys.back().position;
}

float interpolateRotation(const std::vector<Keyframe>& keys, float t) {
    if (keys.empty()) return 0.0f;
    if (t <= keys[0].time) return keys[0].rotation;
    if (t >= keys.back().time) return keys.back().rotation;

    for (size_t i = 0; i < keys.size() - 1; i++) {
        if (t >= keys[i].time && t <= keys[i + 1].time) {
            float alpha = (t - keys[i].time) / (keys[i + 1].time - keys[i].time);
            return glm::mix(keys[i].rotation, keys[i + 1].rotation, alpha);
        }
    }
    return keys.back().rotation;
}


void Animation();

// ====== ESTADO GLOBAL ======
const GLuint WIDTH = 800, HEIGHT = 600;
int SCREEN_WIDTH, SCREEN_HEIGHT;
Camera  camera(glm::vec3(0.0f, 0.0f, 3.0f));
GLfloat lastX = WIDTH / 2.0f, lastY = HEIGHT / 2.0f; bool keys[1024]{}; bool firstMouse = true;
glm::vec3 lightPos(0.0f), Light1(0.0f);

// Piso y lifts
const float FLOOR_Y = 1.2f;  // altura de tu piso
const float LIFT = 1.32f;    // pequeño desfase para evitar z-fighting

// Cubo para debug (sin UV)
float vertices[] = {
    -0.5f,-0.5f,-0.5f, 0,0,-1,  0.5f,-0.5f,-0.5f, 0,0,-1,  0.5f,0.5f,-0.5f, 0,0,-1,
     0.5f,0.5f,-0.5f, 0,0,-1,  -0.5f,0.5f,-0.5f, 0,0,-1, -0.5f,-0.5f,-0.5f,0,0,-1,
    -0.5f,-0.5f, 0.5f,0,0,1,   0.5f,-0.5f, 0.5f,0,0,1,   0.5f,0.5f, 0.5f,0,0,1,
     0.5f,0.5f, 0.5f,0,0,1,   -0.5f,0.5f, 0.5f,0,0,1,  -0.5f,-0.5f, 0.5f,0,0,1,
    -0.5f,0.5f, 0.5f,-1,0,0,  -0.5f,0.5f,-0.5f,-1,0,0, -0.5f,-0.5f,-0.5f,-1,0,0,
    -0.5f,-0.5f,-0.5f,-1,0,0, -0.5f,-0.5f, 0.5f,-1,0,0, -0.5f,0.5f, 0.5f,-1,0,0,
     0.5f,0.5f, 0.5f,1,0,0,    0.5f,0.5f,-0.5f,1,0,0,   0.5f,-0.5f,-0.5f,1,0,0,
     0.5f,-0.5f,-0.5f,1,0,0,   0.5f,-0.5f, 0.5f,1,0,0,  0.5f,0.5f, 0.5f,1,0,0,
    -0.5f,-0.5f,-0.5f,0,-1,0,  0.5f,-0.5f,-0.5f,0,-1,0, 0.5f,-0.5f, 0.5f,0,-1,0,
     0.5f,-0.5f, 0.5f,0,-1,0, -0.5f,-0.5f, 0.5f,0,-1,0,-0.5f,-0.5f,-0.5f,0,-1,0,
    -0.5f, 0.5f,-0.5f,0,1,0,   0.5f, 0.5f,-0.5f,0,1,0,  0.5f, 0.5f, 0.5f,0,1,0,
     0.5f, 0.5f, 0.5f,0,1,0,  -0.5f, 0.5f, 0.5f,0,1,0, -0.5f, 0.5f,-0.5f,0,1,0
};

// Quad 1x1 (pos, normal, uv)
float quadData[] = {
    -0.5f,-0.5f,0.0f,   0,0,1,     0,0,
     0.5f,-0.5f,0.0f,   0,0,1,     1,0,
     0.5f, 0.5f,0.0f,   0,0,1,     1,1,
     0.5f, 0.5f,0.0f,   0,0,1,     1,1,
    -0.5f, 0.5f,0.0f,   0,0,1,     0,1,
    -0.5f,-0.5f,0.0f,   0,0,1,     0,0
};

// Cubo UV (pedestal)
float pedestalCube[] = {
    // -Z
    -0.5f,-0.5f,-0.5f, 0,0,-1, 0,0,  0.5f,-0.5f,-0.5f, 0,0,-1, 1,0,  0.5f,0.5f,-0.5f, 0,0,-1, 1,1,
     0.5f,0.5f,-0.5f, 0,0,-1, 1,1, -0.5f,0.5f,-0.5f, 0,0,-1, 0,1, -0.5f,-0.5f,-0.5f, 0,0,-1, 0,0,
     // +Z
     -0.5f,-0.5f, 0.5f, 0,0,1, 0,0,   0.5f,-0.5f, 0.5f, 0,0,1, 1,0,   0.5f,0.5f, 0.5f, 0,0,1, 1,1,
      0.5f,0.5f, 0.5f, 0,0,1, 1,1,  -0.5f,0.5f, 0.5f, 0,0,1, 0,1,  -0.5f,-0.5f, 0.5f, 0,0,1, 0,0,
      // -X
      -0.5f, 0.5f, 0.5f,-1,0,0, 1,1, -0.5f, 0.5f,-0.5f,-1,0,0, 0,1, -0.5f,-0.5f,-0.5f,-1,0,0, 0,0,
      -0.5f,-0.5f,-0.5f,-1,0,0, 0,0, -0.5f,-0.5f, 0.5f,-1,0,0, 1,0, -0.5f, 0.5f, 0.5f,-1,0,0, 1,1,
      // +X
       0.5f, 0.5f, 0.5f, 1,0,0, 1,1,  0.5f, 0.5f,-0.5f, 1,0,0, 0,1,  0.5f,-0.5f,-0.5f, 1,0,0, 0,0,
       0.5f,-0.5f,-0.5f, 1,0,0, 0,0,  0.5f,-0.5f, 0.5f, 1,0,0, 1,0,  0.5f, 0.5f, 0.5f, 1,0,0, 1,1,
       // -Y
       -0.5f,-0.5f,-0.5f, 0,-1,0, 0,1,  0.5f,-0.5f,-0.5f, 0,-1,0, 1,1,  0.5f,-0.5f, 0.5f, 0,-1,0, 1,0,
        0.5f,-0.5f, 0.5f, 0,-1,0, 1,0, -0.5f,-0.5f, 0.5f, 0,-1,0, 0,0, -0.5f,-0.5f,-0.5f, 0,-1,0, 0,1,
        // +Y
        -0.5f, 0.5f,-0.5f, 0,1,0, 0,1,  0.5f, 0.5f,-0.5f, 0,1,0, 1,1,  0.5f, 0.5f, 0.5f, 0,1,0, 1,0,
         0.5f, 0.5f, 0.5f, 0,1,0, 1,0, -0.5f, 0.5f, 0.5f, 0,1,0, 0,0, -0.5f, 0.5f,-0.5f, 0,1,0, 0,1
};

// === SKYBOX data ===
float skyboxVertices[] = {
    -1.0f,  1.0f, -1.0f,  -1.0f, -1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,   1.0f,  1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,  -1.0f, -1.0f, -1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f,  1.0f, -1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
     1.0f, -1.0f, -1.0f,   1.0f, -1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f,  1.0f, -1.0f,   1.0f, -1.0f, -1.0f,
    -1.0f, -1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,   1.0f, -1.0f,  1.0f,  -1.0f, -1.0f,  1.0f,
    -1.0f,  1.0f, -1.0f,   1.0f,  1.0f, -1.0f,   1.0f,  1.0f,  1.0f,
     1.0f,  1.0f,  1.0f,  -1.0f,  1.0f,  1.0f,  -1.0f,  1.0f, -1.0f,
    -1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f, -1.0f,
     1.0f, -1.0f, -1.0f,  -1.0f, -1.0f,  1.0f,   1.0f, -1.0f,  1.0f
};

// anim/delta
float rotBall = 0.0f; bool AnimBall = false; bool AnimDog = false; float rotDog = 0.0f;
int dogAnim = 0; float FLegs = 0, RLegs = 0, head = 0, tail = 0; glm::vec3 dogPos(0); float dogRot = 0; bool step = false; float pikachuTime = 0.0f;
bool pikachuAnim = false; bool toadAnim = false;
float toadTime = 0.0f; float crashTime = 0.0f;
bool crashAnim = false; float consoleRotation = 0.0f;
float limite = 2.2f;
GLfloat deltaTime = 0.0f, lastFrame = 0.0f;

// VAOs/VBOs
GLuint lampVBO = 0, lampVAO = 0;
GLuint quadVAO = 0, quadVBO = 0;
GLuint pedVAO = 0, pedVBO = 0;

// Skybox
GLuint skyVAO = 0, skyVBO = 0;
GLuint texSkybox = 0;

// Texturas
GLuint texSign = 0, texArrows = 0, texWoodFloor = 0, texWall = 0, texPedestal = 0;

// === Loader de cubemap con SOIL2 ===
static GLuint LoadCubemapSOIL(
    const char* px, const char* nx,
    const char* py, const char* ny,
    const char* pz, const char* nz)
{
    GLuint id = SOIL_load_OGL_cubemap(
        px, nx, py, ny, pz, nz,
        SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID,
        SOIL_FLAG_MIPMAPS | SOIL_FLAG_COMPRESS_TO_DXT
    );
    if (!id) {
        std::cout << "Fallo cubemap: " << SOIL_last_result() << "\n";
        return 0;
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return id;
}

// util: forward vector desde yaw (grados)
static glm::vec3 ForwardFromYaw(float deg) {
    float r = glm::radians(deg);
    return glm::normalize(glm::vec3(std::sin(r), 0.0f, -std::cos(r)));
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_DEPTH_BITS, 24);
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Proyecto Final", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 0; }
    glfwMakeContextCurrent(window);
    glfwGetFramebufferSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseCallback);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) { std::cout << "GLEW fail\n"; return 0; }

    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE); // importante si usas escalas negativas para "flip"
    glDisable(GL_BLEND);

    // Escribir shaders embebidos
    WriteTextFile("Shader/_skin_runtime.vs", SKIN_VS_SRC);
    WriteTextFile("Shader/_tex_runtime.frag", TEX_FRAG_SRC);
    WriteTextFile("Shader/_color_runtime.vs", COLOR_VS_SRC);
    WriteTextFile("Shader/_color_runtime.frag", COLOR_FRAG_SRC);
    WriteTextFile("Shader/_quad_runtime.vs", QUAD_VS_SRC);
    WriteTextFile("Shader/_quad_runtime.frag", QUAD_FRAG_SRC);
    WriteTextFile("Shader/_skybox_runtime.vs", SKYBOX_VS_SRC);
    WriteTextFile("Shader/_skybox_runtime.frag", SKYBOX_FRAG_SRC);

    Shader lightingShader("Shader/lighting.vs", "Shader/lighting.frag");
    Shader lampShader("Shader/lamp.vs", "Shader/lamp.frag");
    Shader skinnedShader("Shader/_skin_runtime.vs", "Shader/_tex_runtime.frag");
    Shader colorShader("Shader/_color_runtime.vs", "Shader/_color_runtime.frag");
    Shader quadShader("Shader/_quad_runtime.vs", "Shader/_quad_runtime.frag");
    Shader skyShader("Shader/_skybox_runtime.vs", "Shader/_skybox_runtime.frag");
    skyShader.Use();
    glUniform1i(glGetUniformLocation(skyShader.Program, "skybox"), 0);

    // Modelos

    //Modelo de la galeria
    Model escenario((char*)"Models/wip-gallery-v0003/source/GalleryModel_v0003/GalleryModel_v0007.obj");

    //Sala 1
    Model arc1((char*)"Models/arcade_machine.obj");
    Model arc2((char*)"Models/game_machine_0000001.obj");
    Model arc3((char*)"Models/Super_Famicom_Console_1105070442_texture.obj");
    Model arc4((char*)"Models/GameBoy_1105065316_texture.obj");
    Model arc5((char*)"Models/Atari_Console_Classic_1105064245_texture.obj");
    Model arc7((char*)"Models/Hay_un_cuadro_de_pint_1106084744_texture.obj");
    Model arc8((char*)"Models/bench.obj");
    Model arc9((char*)"Models/pacman_model.obj");
    Model arc10((char*)"Models/mario_model.obj");
    Model arc11((char*)"Models/petit.obj");
    Model arc12((char*)"Models/dkstatue.obj");

    //Sala 2F
    Model CuboBase1((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model CuboBase2((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model CuboBase3((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model crash((char*)"Models/Sala2/CrashBandicoot/Animation_Crawl_and_Look_Back_withSkin.fbx");  // Tu archivo FBX de Crash
    Model xboxSX((char*)"Models/Sala2/XboxSeriesX/_1106040925_texture.obj");
    Model Nswitch((char*)"Models/Sala2/nintendo-switch/_1106051703_texture.obj");
    Model PS5((char*)"Models/Sala2/ps5/PS5/_1112073936_texture.obj");
    Model XboxLogo((char*)"Models/Sala2/XboxLogo/Screenshot_2025_11_07_1108045114_texture.obj");
    Model NswitchLogo((char*)"Models/Sala2/NintendoLogo/_1108052044_texture.obj");
    Model PS5Logo((char*)"Models/Sala2/PlayStationLogo/PS_1108045851_texture.obj");
    Model xboxControl("Models/Sala2/xboxcco/source/xboxcco/xboxcco/xboxcc.obj");
    Model ps5Control("Models/Sala2/PS5C/_1111070337_texture_obj/_1111070337_texture.obj");


             // Modelos de Pikachu
        Model pikachu("Models//Pikachu/Pikachu.obj");
        Model banquito("Models/Pikachu/banquito.obj");
        Model cola("Models/Pikachu/Cola.obj");

        // Modelos de Toad
        Model toadCuerpo((char*)"Models/Sala2/Toad/toad_cuerpo.obj");
        Model toadBrazoIzq((char*)"Models/Sala2/Toad/toad_b_izq.obj");
        Model toadBrazoDer((char*)"Models/Sala2/Toad/toad_b_der.obj");




    //Sala3 
    Model game((char*)"Models/sala3/Game_ready_3D_prop_a_1110045024_texture.obj");
    Model vr((char*)"Models/sala3/VR_headset_with_two_m_1105231651_texture.obj");
    Model warrior((char*)"Models/sala3/Animation_Walking_withSkin.fbx");
    Model console((char*)"Models/sala3/Game_ready_3D_prop_a_1110065502_texture.obj");
	Model controller((char*)"Models/sala3/Game_Controllers_Disp_1110071455_texture.obj");
	Model yoda((char*)"Models/sala3/Animation_Alert_withSkin.fbx");
	Model truper((char*)"Models/sala3/Animation_Forward_Roll_and_Fire_withSkin.fbx");
	Model wall((char*)"Models/sala3/wall_acoustic_pane_1112003419_texture.obj");
    Model ceiling((char*)"Models/sala3/ceiling_track_light__1112003755_texture.obj");
    Model ceiling2((char*)"Models/sala3/ceiling_track_light__1112003755_texture.obj");
    Model ceiling3((char*)"Models/sala3/ceiling_track_light__1112003755_texture.obj");
	Model astro((char*)"Models/sala3/Animation_Agree_Gesture_withSkin.fbx");
	Model kratos((char*)"Models/sala3/Animation_Axe_Spin_Attack_withSkin.fbx");
	Model link((char*)"Models/sala3/Animation_Big_Wave_Hello_withSkin.fbx");
    Model CuboBase4((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model CuboBase5((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");

    // VAO cubo debug
    glGenVertexArrays(1, &lampVAO);
    glGenBuffers(1, &lampVBO);
    glBindVertexArray(lampVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lampVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);                   glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // VAO quad
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadData), quadData, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);                 glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // VAO/VBO pedestal
    glGenVertexArrays(1, &pedVAO);
    glGenBuffers(1, &pedVBO);
    glBindVertexArray(pedVAO);
    glBindBuffer(GL_ARRAY_BUFFER, pedVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(pedestalCube), pedestalCube, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);                 glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float))); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float))); glEnableVertexAttribArray(2);
    glBindVertexArray(0);

    // Skybox
    glGenVertexArrays(1, &skyVAO);
    glGenBuffers(1, &skyVBO);
    glBindVertexArray(skyVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Samplers
    lightingShader.Use();
    glUniform1i(glGetUniformLocation(lightingShader.Program, "material.diffuse"), 0);
    glUniform1i(glGetUniformLocation(lightingShader.Program, "material.specular"), 1);
    glUniform1f(glGetUniformLocation(lightingShader.Program, "material.shininess"), 16.0f);
    glUniform1i(glGetUniformLocation(lightingShader.Program, "transparency"), 0);

    skinnedShader.Use();
    glUniform1i(glGetUniformLocation(skinnedShader.Program, "texture_diffuse1"), 0);

    // Texturas 2D
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    texSign = SOIL_load_OGL_texture("Models/sala3/vr_sign.png", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texArrows = SOIL_load_OGL_texture("Models/sala3/floor_arrows.png", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texWoodFloor = SOIL_load_OGL_texture("Models/sala3/wood_floor.jpg", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texWall = SOIL_load_OGL_texture("Models/sala3/wall_concrete.jpg", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texPedestal = SOIL_load_OGL_texture("Models/sala3/pedestal_charcoal.png", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);

    if (!texSign)      std::cout << "No se cargo Models/sala3/vr_sign.png\n";
    if (!texArrows)    std::cout << "No se cargo Models/sala3/floor_arrows.png\n";
    if (!texWoodFloor) std::cout << "No se cargo Models/sala3/wood_floor.jpg\n";
    if (!texWall)      std::cout << "No se cargo Models/sala3/wall_concrete.jpg\n";
    if (!texPedestal)  std::cout << "No se cargo Models/sala3/pedestal_charcoal.png\n";

    auto setupRepeat = [](GLuint id) {
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        };
    setupRepeat(texSign);
    setupRepeat(texArrows);
    setupRepeat(texWoodFloor);
    setupRepeat(texWall);
    setupRepeat(texPedestal);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Skybox
    texSkybox = LoadCubemapSOIL(
        "Models/sala3/skybox/right.png",
        "Models/sala3/skybox/left.png",
        "Models/sala3/skybox/top.png",
        "Models/sala3/skybox/bot.png",
        "Models/sala3/skybox/front.png",
        "Models/sala3/skybox/back.png"
    );
    if (!texSkybox) std::cout << "No se pudo cargar el skybox\n";

    // =============================== 
    // AJUSTES RÁPIDOS — ESTACIÓN VR
    // (MUEVE SOLO ESTOS VALORES)w
    // ===============================

    // Headset VR
    glm::vec3 VR_HEADSET_POS = glm::vec3(-32.0f, 4.5f, -13.0f);
    float     VR_HEADSET_YAW = 360.0f;   // grados sobre Y
    float     VR_HEADSET_SCL = 10.0f;

    // Pedestal (para que "asiente" en el piso, usa Y = FLOOR_Y + (alto/2))
    glm::vec3 PEDESTAL_POS = glm::vec3(-32.0f, FLOOR_Y + 0.30f, -10.0f);
    glm::vec3 PEDESTAL_SCALE = glm::vec3(0.6f, 0.6f, 0.6f); // alto = 0.6
    float     PEDESTAL_YAW = 0.0f;

    // Flechas en el piso
    glm::vec3 ARROWS_POS = glm::vec3(-32.0f, FLOOR_Y + 0.001f, -10.0f);
    float     ARROWS_YAW = 180.0f;
    glm::vec2 ARROWS_TILING = glm::vec2(1.6f, 0.9f);

    // Rótulo (texto)
    glm::vec3 SIGN_POS = glm::vec3(-32.0f, 3.0f, -10.0f);
    float     SIGN_YAW = 180.0f;
    glm::vec2 SIGN_SIZE = glm::vec2(1.00f, 0.18f);
    float     SIGN_Z_BIAS = 0.04f;      // separarlo del muro en su "forward"
    bool      SIGN_FLIP_X = false;      // pon true si lo ves espejeado

    // Backplate detrás del rótulo
    glm::vec3 BACK_POS = glm::vec3(-32.0f, 3.0f, -10.06f);
    float     BACK_YAW = 180.0f;
    glm::vec3 BACK_SCALE = glm::vec3(1.20f, 0.28f, 0.05f);

    // Glow (planchita frente al backplate)
    glm::vec3 GLOW_POS = glm::vec3(-32.0f, 3.0f, -10.03f);
    float     GLOW_YAW = 180.0f;
    glm::vec3 GLOW_SCALE = glm::vec3(1.10f, 0.20f, 0.02f);

    // ===============================

    static double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime(); deltaTime = currentFrame - lastFrame; lastFrame = currentFrame;
        glfwPollEvents(); DoMovement(); Animation();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.GetZoom()), (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, 0.5f, 50.0f);
        glm::mat4 view = camera.GetViewMatrix();

        // ====== MODELOS Y ESCENARIO ======
        lightingShader.Use();
        GLint modelLoc = glGetUniformLocation(lightingShader.Program, "model");
        GLint viewLoc = glGetUniformLocation(lightingShader.Program, "view");
        GLint projLoc = glGetUniformLocation(lightingShader.Program, "projection");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        // ===== CONFIGURAR SPOTLIGHTS =====
// Spotlight 0: Xbox (VERDE)
        {
            glm::vec3 xboxLogoPos = glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 3.0f);
            glm::vec3 xboxConsolePos = glm::vec3(5.0f, FLOOR_Y + LIFT + 1.30f, 3.0f);
            glm::vec3 spotDir = glm::normalize(xboxConsolePos - xboxLogoPos);

            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[0].position"),
                xboxLogoPos.x, xboxLogoPos.y, xboxLogoPos.z);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[0].direction"),
                spotDir.x, spotDir.y, spotDir.z);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[0].ambient"),
                0.1f, 0.3f, 0.1f);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[0].diffuse"),
                0.5f, 2.0f, 0.5f);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[0].specular"),
                0.2f, 0.8f, 0.2f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[0].cutOff"),
                glm::cos(glm::radians(15.5f)));
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[0].outerCutOff"),
                glm::cos(glm::radians(20.5f)));
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[0].constant"), 1.0f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[0].linear"), 0.045f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[0].quadratic"), 0.0075f);
        }

        // Spotlight 1: Nintendo (ROJO)
        {
            glm::vec3 nintendoLogoPos = glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 15.0f);
            glm::vec3 nintendoConsolePos = glm::vec3(5.0f, FLOOR_Y + LIFT + 1.10f, 15.0f);
            glm::vec3 spotDir = glm::normalize(nintendoConsolePos - nintendoLogoPos);

            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[1].position"),
                nintendoLogoPos.x, nintendoLogoPos.y, nintendoLogoPos.z);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[1].direction"),
                spotDir.x, spotDir.y, spotDir.z);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[1].ambient"),
                0.3f, 0.1f, 0.1f);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[1].diffuse"),
                2.0f, 0.5f, 0.5f);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[1].specular"),
                0.8f, 0.2f, 0.2f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[1].cutOff"),
                glm::cos(glm::radians(15.5f)));
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[1].outerCutOff"),
                glm::cos(glm::radians(20.5f)));
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[1].constant"), 1.0f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[1].linear"), 0.045f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[1].quadratic"), 0.0075f);
        }

        // Spotlight 2: PS5 (AZUL)
        {
            glm::vec3 ps5LogoPos = glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 26.0f);
            glm::vec3 ps5ConsolePos = glm::vec3(5.0f, FLOOR_Y + LIFT + 0.90f, 26.0f);
            glm::vec3 spotDir = glm::normalize(ps5ConsolePos - ps5LogoPos);

            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[2].position"),
                ps5LogoPos.x, ps5LogoPos.y, ps5LogoPos.z);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[2].direction"),
                spotDir.x, spotDir.y, spotDir.z);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[2].ambient"),
                0.1f, 0.1f, 0.3f);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[2].diffuse"),
                0.5f, 0.8f, 2.5f);
            glUniform3f(glGetUniformLocation(lightingShader.Program, "spotLights[2].specular"),
                0.2f, 0.4f, 1.0f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[2].cutOff"),
                glm::cos(glm::radians(15.5f)));
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[2].outerCutOff"),
                glm::cos(glm::radians(20.5f)));
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[2].constant"), 1.0f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[2].linear"), 0.045f);
            glUniform1f(glGetUniformLocation(lightingShader.Program, "spotLights[2].quadratic"), 0.0075f);
        }
        // ===== FIN SPOTLIGHTS =====

        // Escenario
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(4.0f, FLOOR_Y - LIFT, -16.0f));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            escenario.Draw(lightingShader);
        }

        // Son los modelos de la sala 1

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-23.0f, FLOOR_Y + LIFT, 29.0f));
            m = glm::scale(m, glm::vec3(1.1f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc1.Draw(lightingShader);
        }

		// Maquina de arcade azul
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-19.0f, FLOOR_Y + LIFT, 29.0f));
            m = glm::scale(m, glm::vec3(0.07f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc2.Draw(lightingShader);
        }

		// Super Nintendo
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 3.29f, 32.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(1.1f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc3.Draw(lightingShader);
        }

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 2.1, 32.0f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase1.Draw(lightingShader);
        }

		// GameBoy
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 3.9f, 36.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc4.Draw(lightingShader);
        }

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 2.1, 36.0f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase1.Draw(lightingShader);
        }

		// Atari
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-18.0f, FLOOR_Y + 3.5, 49.0f));
            m = glm::rotate(m, glm::radians(25.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(1.1f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc5.Draw(lightingShader);
        }

        // Mesa blanca
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-18.0f, FLOOR_Y + 2.1, 49.0f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase1.Draw(lightingShader);
        }

        // Atari con television
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 3.32, 28.0f));
            m = glm::rotate(m, glm::radians(25.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(1.1f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc7.Draw(lightingShader);
        }

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 2.1, 28.0f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase1.Draw(lightingShader);
        }

        // Banca retro
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-24.0f, FLOOR_Y + 2.0, 39.0f));
            m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(1.6f, 2.0f, 1.5f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc8.Draw(lightingShader);
        }

        //Pacman

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-19.5f, FLOOR_Y + LIFT, 55.0f));
            m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.3f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc9.Draw(lightingShader);
        }

        //Mario Bros

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-26.0f, FLOOR_Y + LIFT, 54.0f));
            m = glm::rotate(m, glm::radians(155.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.03f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc10.Draw(lightingShader);
        }


        // Fantasmita

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-29.0f, FLOOR_Y + 3.0f, 50.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc11.Draw(lightingShader);
        }

        // Estatua Donkey Kong

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-27.0f, FLOOR_Y + LIFT, 45.0f));
            m = glm::rotate(m, glm::radians(25.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.2f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc12.Draw(lightingShader);
        }



        



// Cubo base 1
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.2f - 0.55f, 3.0f));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase1.Draw(lightingShader);
        }
        // Cubo base 2
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.2f - 0.55f, 15.0f));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase2.Draw(lightingShader);
        }
        // Cubo base 3
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.2f - 0.55f, 26.0f));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase3.Draw(lightingShader);
        }
        // Xbox SX - con rotación
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.30f+1.2f - 0.55f, 3.0f));
            m = glm::rotate(m, glm::radians(consoleRotation), glm::vec3(0, 1, 0)); // Rotación sobre Y
            m = glm::scale(m, glm::vec3(0.5f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            xboxSX.Draw(lightingShader);
        }
        // Control Xbox en CuboBase1
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.5f, FLOOR_Y + LIFT + 2.5f - 0.95f, 3.6f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::rotate(m, glm::radians(-80.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            xboxControl.Draw(lightingShader);
        }


        // Switch - con rotación
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.10f + 1.2f - 0.55f, 15.0f));
            m = glm::rotate(m, glm::radians(consoleRotation), glm::vec3(0, 1, 0)); // Rotación sobre Y
            m = glm::scale(m, glm::vec3(0.5f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            Nswitch.Draw(lightingShader);
        }

        // PS5 - con rotación
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.40f + 1.2f -0.62f, 26.0f));
            m = glm::rotate(m, glm::radians(consoleRotation), glm::vec3(0, 1, 0)); // Rotación sobre Y
            m = glm::scale(m, glm::vec3(0.5f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            PS5.Draw(lightingShader);
        }

        // Control PS5 en CuboBase3
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.4f, FLOOR_Y + LIFT + 2.5f - 0.95f, 26.5f));
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            m = glm::scale(m, glm::vec3(0.25f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            ps5Control.Draw(lightingShader);
        }



        // XboxLogo - CON EMISIÓN VERDE
        {
            // Activar emisión verde brillante
            glUniform3f(glGetUniformLocation(lightingShader.Program, "emissiveColor"),
                0.2f, 1.0f, 0.2f); // Verde brillante
            glUniform1f(glGetUniformLocation(lightingShader.Program, "emissiveStrength"),
                3.0f); // Intensidad alta

            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 3.0f));
            m = glm::scale(m, glm::vec3(1.5f));
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            XboxLogo.Draw(lightingShader);

            // Desactivar emisión después de dibujar
            glUniform1f(glGetUniformLocation(lightingShader.Program, "emissiveStrength"), 0.0f);
        }

        // NswitchLogo - CON EMISIÓN ROJA
        {
            // Activar emisión roja brillante
            glUniform3f(glGetUniformLocation(lightingShader.Program, "emissiveColor"),
                1.0f, 0.2f, 0.2f); // Rojo brillante
            glUniform1f(glGetUniformLocation(lightingShader.Program, "emissiveStrength"),
                3.0f); // Intensidad alta

            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 14.8f));
            m = glm::scale(m, glm::vec3(3.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            NswitchLogo.Draw(lightingShader);

            // Desactivar emisión después de dibujar
            glUniform1f(glGetUniformLocation(lightingShader.Program, "emissiveStrength"), 0.0f);
        }

        // PS5Logo - CON EMISIÓN AZUL
        {
            // Activar emisión azul brillante
            glUniform3f(glGetUniformLocation(lightingShader.Program, "emissiveColor"),
                0.3f, 0.5f, 1.5f); // Azul brillante
            glUniform1f(glGetUniformLocation(lightingShader.Program, "emissiveStrength"),
                1.0f); // Intensidad alta

            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 26.0f));
            m = glm::scale(m, glm::vec3(1.5f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            PS5Logo.Draw(lightingShader);

            // Desactivar emisión después de dibujar
            glUniform1f(glGetUniformLocation(lightingShader.Program, "emissiveStrength"), 0.0f);
        }


        //termina sala 2

        




        //Sala 3



        // ====== VR HEADSET (posición separada) ======
        {
            lightingShader.Use();
            GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model");
            glm::mat4 m(1.0f);
            m = glm::translate(m, VR_HEADSET_POS);
            m = glm::rotate(m, glm::radians(VR_HEADSET_YAW), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(VR_HEADSET_SCL));
            glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            vr.Draw(lightingShader);
        }
        //Cubo base 4
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-32.0f, FLOOR_Y + LIFT+.8, -13.0f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase4.Draw(lightingShader);
        }

        // ====== Guerrero skinned ======
        skinnedShader.Use();
        GLint sModelLoc = glGetUniformLocation(skinnedShader.Program, "model");
        GLint sViewLoc = glGetUniformLocation(skinnedShader.Program, "view");
        GLint sProjLoc = glGetUniformLocation(skinnedShader.Program, "projection");
        glUniformMatrix4fv(sViewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(sProjLoc, 1, GL_FALSE, glm::value_ptr(projection));
        {
            double t = glfwGetTime() - t0;
            warrior.UpdateAnimation(t);
            std::vector<glm::mat4> bones; warrior.GetBoneMatrices(bones, 100);
            GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
            if (bonesLoc >= 0 && !bones.empty())
                glUniformMatrix4fv(bonesLoc, (GLsizei)bones.size(), GL_FALSE, &bones[0][0][0]);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-22.0f, FLOOR_Y + LIFT, 8.5f));
            m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            warrior.Draw(skinnedShader);
        }

        // ====== Yoda animacion ======
        skinnedShader.Use();
        
        {
            double t = glfwGetTime() - t0;
            yoda.UpdateAnimation(t);
            std::vector<glm::mat4> bones; yoda.GetBoneMatrices(bones, 100);
            GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
            if (bonesLoc >= 0 && !bones.empty())
                glUniformMatrix4fv(bonesLoc, (GLsizei)bones.size(), GL_FALSE, &bones[0][0][0]);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-25.0f, FLOOR_Y + LIFT, 0.5f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            yoda.Draw(skinnedShader);
        }

        // ====== truper animacion ======
        skinnedShader.Use();

        {
            double t = glfwGetTime() - t0;
            truper.UpdateAnimation(t);
            std::vector<glm::mat4> bones; truper.GetBoneMatrices(bones, 100);
            GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
            if (bonesLoc >= 0 && !bones.empty())
                glUniformMatrix4fv(bonesLoc, (GLsizei)bones.size(), GL_FALSE, &bones[0][0][0]);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-25.0f, FLOOR_Y + LIFT, 21.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
           truper.Draw(skinnedShader);
        }


        // ====== Astro animacion ======
        skinnedShader.Use();

        {
            double t = glfwGetTime() - t0;
            astro.UpdateAnimation(t);
            std::vector<glm::mat4> bones; astro.GetBoneMatrices(bones, 100);
            GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
            if (bonesLoc >= 0 && !bones.empty())
                glUniformMatrix4fv(bonesLoc, (GLsizei)bones.size(), GL_FALSE, &bones[0][0][0]);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-25.0f, FLOOR_Y + LIFT, 12.0f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            astro.Draw(skinnedShader);
        }

        // ====== Kratos animacion ======
        skinnedShader.Use();

        {
            double t = glfwGetTime() - t0;
            kratos.UpdateAnimation(t);
            std::vector<glm::mat4> bones; kratos.GetBoneMatrices(bones, 100);
            GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
            if (bonesLoc >= 0 && !bones.empty())
                glUniformMatrix4fv(bonesLoc, (GLsizei)bones.size(), GL_FALSE, &bones[0][0][0]);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-25.0f, FLOOR_Y + LIFT-.25, -12.0f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.025f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            kratos.Draw(skinnedShader);
        }

        // ====== link animacion ======
        skinnedShader.Use();

        {
            double t = glfwGetTime() - t0;
            link.UpdateAnimation(t);
            std::vector<glm::mat4> bones; link.GetBoneMatrices(bones, 100);
            GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
            if (bonesLoc >= 0 && !bones.empty())
                glUniformMatrix4fv(bonesLoc, (GLsizei)bones.size(), GL_FALSE, &bones[0][0][0]);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(-25.0f, FLOOR_Y + LIFT-.38, -5.0f));
            m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.025f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            link.Draw(skinnedShader);
        }

        // ====== game (prop de sala) — CORREGIDO translate ======
        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-18.0f, 4.2, 2.5f)); 
            m = glm::rotate(m, glm::radians(270.0f), glm::vec3(0, 1, 0)); 
            m = glm::scale(m, glm::vec3(1.5f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m)); 
            game.Draw(lightingShader); 
        }

        //Cubo base 4
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(-18.0f, FLOOR_Y + LIFT+.8, 2.5f));
            m = glm::scale(m, glm::vec3(0.9f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase5.Draw(lightingShader);
        }

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-20.0f, 5.0f , -13.0f)); 
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0)); 
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m)); 
            console.Draw(lightingShader); 
        }

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-20.0f, FLOOR_Y + LIFT+1.8, 13.0f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            controller.Draw(lightingShader);
        }

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-35.0f, 6.0f, 17.0f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            wall.Draw(lightingShader);
        }

        //Lampara 1

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-25.0f, 8.3f, 22.0f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            ceiling.Draw(lightingShader);
        }

        //Lampara 2

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-25.0f, 8.3f, 9.0f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            ceiling2.Draw(lightingShader);
        }

        //Lampara 3

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-25.0f, 8.3f, -4.0f));
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            ceiling3.Draw(lightingShader);
        }


        // ===== ANIMACIÓN PIKACHU POR KEYFRAMES =====
        // Keyframes de Pikachu (salto del banco)
        std::vector<Keyframe> pikachuKeys = {
     {0.0f,  glm::vec3(10.0f, FLOOR_Y + LIFT + 0.53f, 3.0f), 0.0f},      // Sentado EN el banco (mismo X,Z que banco, Y arriba)
     {1.0f,  glm::vec3(10.0f, FLOOR_Y + LIFT + 1.1f, 3.0f), 0.0f},      // Sube (preparación)
     {2.0f,  glm::vec3(10.0f, FLOOR_Y + LIFT + 1.7f, 4.0f), 20.0f},     // En el aire (apex) - salta adelante
     {3.5f,  glm::vec3(10.0f, FLOOR_Y + LIFT + 0.4f, 5.0f), 0.0f},      // Aterriza adelante
     {5.0f,  glm::vec3(10.0f, FLOOR_Y + LIFT - 0.12f, 5.0f), 0.0f}       // Reposo
        };

  




        glm::vec3 pikachuPos = interpolatePosition(pikachuKeys, pikachuTime);
        float pikachuRot = interpolateRotation(pikachuKeys, pikachuTime);

        // Movimiento de la cola (oscilación sinusoidal)
        float tailSwing = glm::sin(pikachuTime * 5.0f) * 30.0f;

        lightingShader.Use();
        GLint modelLocPika = glGetUniformLocation(lightingShader.Program, "model");

        // Dibujar banco

        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(10.0f, FLOOR_Y + LIFT + 0.2f, 3.0f)); // Subir un poco
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            m = glm::scale(m, glm::vec3(0.1f, 0.1f, 0.1f)); // Aumentar escala
            glUniformMatrix4fv(modelLocPika, 1, GL_FALSE, glm::value_ptr(m));
            banquito.Draw(lightingShader);
        }



        // Dibujar Pikachu (cuerpo principal)

        {

            
                glm::mat4 m(1);
                m = glm::translate(m, pikachuPos);
                m = glm::rotate(m, glm::radians(0.0f + pikachuRot), glm::vec3(0, 1, 0)); // +90 grados extra
                m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(1, 0, 0));
                m = glm::scale(m, glm::vec3(0.15f));

            glUniformMatrix4fv(modelLocPika, 1, GL_FALSE, glm::value_ptr(m));
            pikachu.Draw(lightingShader);
        }


        // Dibujar cola con movimiento (adherida al cuerpo)
        {
            glm::mat4 m(1);
            m = glm::translate(m, pikachuPos);
            m = glm::rotate(m, glm::radians(0.0f + pikachuRot), glm::vec3(0, 1, 0)); // +90 grados extra
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            m = glm::translate(m, glm::vec3(0.0f, 0.05f, 0.25f)); // Cola más cerca del cuerpo (X reducido)
            m = glm::rotate(m, glm::radians(tailSwing), glm::vec3(0, 0, 1));
            m = glm::scale(m, glm::vec3(0.15f));
            glUniformMatrix4fv(modelLocPika, 1, GL_FALSE, glm::value_ptr(m));
            cola.Draw(lightingShader);
        }


        // ===== TOAD CON ANIMACIÓN POR KEYFRAMES =====
        lightingShader.Use();

        glm::vec3 toadBasePos(10.0f, FLOOR_Y + LIFT, 15.0f);



        float armRotLeft = 70.0f, armRotRight = 70.0f;
        float armWaveLeft = 0.0f, armWaveRight = 0.0f;
        float bodyY = 0.0f, bodyRotY = 0.0f;

        if (toadAnim) {
            float t = toadTime;

            armWaveLeft = sin(t * 8.0f) * 25.0f;
            armWaveRight = sin(t * 8.0f + 0.5f) * 25.0f;

            if (t < 1.5f) {
                float progress = t / 1.5f;
                armRotLeft = glm::mix(70.0f, 140.0f, progress);
                armRotRight = glm::mix(70.0f, 140.0f, progress);
                bodyY = sin(progress * 6.28f * 2.0f) * 0.1f;
            }
            else if (t < 2.5f) {
                float progress = (t - 1.5f) / 1.0f;
                armRotLeft = 140.0f;
                armRotRight = 140.0f;
                bodyY = sin(progress * 3.14159f) * 1.2f;
                bodyRotY = progress * 360.0f;
            }
            else if (t < 3.0f) {
                float progress = (t - 2.5f) / 0.5f;
                armRotLeft = glm::mix(140.0f, 110.0f, progress);
                armRotRight = glm::mix(140.0f, 110.0f, progress);
                bodyY = 0.0f;
                bodyRotY = 360.0f;
            }
            else {
                float progress = (t - 3.0f) / 3.0f;
                armRotLeft = 110.0f + sin(progress * 6.28f * 2.0f) * 15.0f;
                armRotRight = 110.0f + sin(progress * 6.28f * 2.0f) * 15.0f;
                bodyY = sin(progress * 6.28f * 4.0f) * 0.08f;
                bodyRotY = 360.0f;
            }

        }

        glm::vec3 toadPos = toadBasePos + glm::vec3(0, bodyY, 0);

        glm::mat4 mToadCuerpo(1.0f);
        mToadCuerpo = glm::translate(mToadCuerpo, toadPos);
        mToadCuerpo = glm::rotate(mToadCuerpo, glm::radians(bodyRotY + 90.0f), glm::vec3(0, 1, 0));
        mToadCuerpo = glm::scale(mToadCuerpo, glm::vec3(0.28f));

        GLint toadModelLoc = glGetUniformLocation(lightingShader.Program, "model");
        glUniformMatrix4fv(toadModelLoc, 1, GL_FALSE, glm::value_ptr(mToadCuerpo));
        toadCuerpo.Draw(lightingShader);

        glm::mat4 mToadBrazoIzq(1.0f);
        mToadBrazoIzq = glm::translate(mToadBrazoIzq, toadPos);
        mToadBrazoIzq = glm::rotate(mToadBrazoIzq, glm::radians(bodyRotY + 90.0f), glm::vec3(0, 1, 0));
        mToadBrazoIzq = glm::translate(mToadBrazoIzq, glm::vec3(0.4f, 0.6f, 0));
        mToadBrazoIzq = glm::rotate(mToadBrazoIzq, glm::radians(-(armRotLeft + armWaveLeft)), glm::vec3(1, 0, 0));
        mToadBrazoIzq = glm::translate(mToadBrazoIzq, glm::vec3(-0.4f, 0.0f, 0));
        mToadBrazoIzq = glm::scale(mToadBrazoIzq, glm::vec3(0.35f));

        glUniformMatrix4fv(toadModelLoc, 1, GL_FALSE, glm::value_ptr(mToadBrazoIzq));
        toadBrazoIzq.Draw(lightingShader);
        
        glm::mat4 mToadBrazoDer(1.0f);
        mToadBrazoDer = glm::translate(mToadBrazoDer, toadPos);
        mToadBrazoDer = glm::rotate(mToadBrazoDer, glm::radians(bodyRotY + 90.0f), glm::vec3(0, 1, 0));
        mToadBrazoDer = glm::translate(mToadBrazoDer, glm::vec3(-0.4f, 0.6f, 0));
        mToadBrazoDer = glm::rotate(mToadBrazoDer, glm::radians(-(armRotRight + armWaveRight)), glm::vec3(1, 0, 0));
        mToadBrazoDer = glm::translate(mToadBrazoDer, glm::vec3(0.4f, 0.0f, 0));
        mToadBrazoDer = glm::scale(mToadBrazoDer, glm::vec3(0.35f));

        glUniformMatrix4fv(toadModelLoc, 1, GL_FALSE, glm::value_ptr(mToadBrazoDer));
        toadBrazoDer.Draw(lightingShader);



        // ===== CRASH DE A A B CON ANIMACIÓN =====
        float travelTime = 10.0f;
        float t = fmod(crashTime, travelTime) / travelTime;
        float crashX = 7.5f + (4.65f * t);

        glm::vec3 crashPos(crashX, FLOOR_Y + LIFT, 26.0f);

        skinnedShader.Use();
        crash.UpdateAnimation(glfwGetTime());

        std::vector<glm::mat4> bones(100, glm::mat4(1.0f));
        crash.GetBoneMatrices(bones, 100);

        GLint bonesLoc = glGetUniformLocation(skinnedShader.Program, "bones");
        glUniformMatrix4fv(bonesLoc, 100, GL_FALSE, &bones[0][0][0]);

        glm::mat4 mCrash(1.0f);
        mCrash = glm::translate(mCrash, crashPos);
        mCrash = glm::rotate(mCrash, 1.5708f, glm::vec3(0, 1, 0));
        mCrash = glm::scale(mCrash, glm::vec3(0.02f));

        GLint crashModelLoc = glGetUniformLocation(skinnedShader.Program, "model");
        glUniformMatrix4fv(crashModelLoc, 1, GL_FALSE, glm::value_ptr(mCrash));

        crash.Draw(skinnedShader);


        // ====== Cubo lámpara (debug) ======
        lampShader.Use();
        GLint ml = glGetUniformLocation(lampShader.Program, "model");
        GLint vl = glGetUniformLocation(lampShader.Program, "view");
        GLint pl = glGetUniformLocation(lampShader.Program, "projection");
        glUniformMatrix4fv(vl, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(pl, 1, GL_FALSE, glm::value_ptr(projection));
        glm::mat4 lampM(1.0f);
        lampM = glm::translate(lampM, lightPos);
        lampM = glm::scale(lampM, glm::vec3(0.2f));
        glUniformMatrix4fv(ml, 1, GL_FALSE, glm::value_ptr(lampM));
        glBindVertexArray(lampVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);

        // ====== SKYBOX ======
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        skyShader.Use();
        glm::mat4 viewNoT = glm::mat4(glm::mat3(view));
        GLint sv = glGetUniformLocation(skyShader.Program, "view");
        GLint sp = glGetUniformLocation(skyShader.Program, "projection");
        glUniformMatrix4fv(sv, 1, GL_FALSE, glm::value_ptr(viewNoT));
        glUniformMatrix4fv(sp, 1, GL_FALSE, glm::value_ptr(projection));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texSkybox);
        glBindVertexArray(skyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);

        glfwSwapBuffers(window);
    }
    glfwTerminate();
    return 0;
}

void DoMovement() {
    if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP])    camera.ProcessKeyboard(FORWARD, deltaTime);
    if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN])  camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT])  camera.ProcessKeyboard(LEFT, deltaTime);
    if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) camera.ProcessKeyboard(RIGHT, deltaTime);
}
void KeyCallback(GLFWwindow* window, int key, int, int action, int)
{
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    if (key >= 0 && key < 1024)
    {
        if (action == GLFW_PRESS) {
            keys[key] = true;

            // Activar animaciones con tecla K
            if (key == GLFW_KEY_K && action == GLFW_PRESS) {
                // Activa/desactiva ambas animaciones de keyframes
                pikachuAnim = !pikachuAnim;
                toadAnim = !toadAnim;

                if (!pikachuAnim) pikachuTime = 0.0f;
                if (!toadAnim) toadTime = 0.0f;
            }


            // Activar animación de Crash con tecla C
            if (key == GLFW_KEY_C) {
                crashAnim = !crashAnim;
                if (crashAnim) crashTime = 0.0f;
                std::cout << "Crash animacion: " << (crashAnim ? "ON" : "OFF") << std::endl;
            }



        }
        else if (action == GLFW_RELEASE) {
            keys[key] = false;
        }
    }
}

void Animation() {
    if (AnimBall) {
        rotBall += 0.04f;
    }
    if (AnimDog) {
        rotDog -= 0.006f;
    }

    // Rotación continua de consolas
    consoleRotation += 0.5f * deltaTime * 60.0f;
    if (consoleRotation > 360.0f) {
        consoleRotation -= 360.0f;
    }

    // Animación de Pikachu
    if (pikachuAnim) {
        pikachuTime += 0.032f;
        if (pikachuTime > 5.0f) {
            pikachuTime = 0.0f;
        }
    }
    // Animación de Toad
    if (toadAnim) {
        toadTime += 0.016f;
        if (toadTime > 6.0f) {
            toadTime = 0.0f;
        }
    }

    // Animación de Crash Bandicoot 
    if (crashAnim) {
        crashTime += 0.016f;
    }
}

void MouseCallback(GLFWwindow*, double x, double y) {
    if (firstMouse) { lastX = (float)x; lastY = (float)y; firstMouse = false; }
    float xo = (float)x - lastX, yo = lastY - (float)y; lastX = (float)x; lastY = (float)y; camera.ProcessMouseMovement(xo, yo);
}
