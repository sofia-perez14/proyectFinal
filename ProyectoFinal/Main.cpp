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
int dogAnim = 0; float FLegs = 0, RLegs = 0, head = 0, tail = 0; glm::vec3 dogPos(0); float dogRot = 0; bool step = false; float limite = 2.2f;
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

    //Sala 2
    Model CuboBase1((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model CuboBase2((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model CuboBase3((char*)"Models/Sala2/Cubo/_1108054346_texture.obj");
    Model xboxSX((char*)"Models/Sala2/XboxSeriesX/_1106040925_texture.obj");
    Model Nswitch((char*)"Models/Sala2/nintendo-switch/_1106051703_texture.obj");
    Model PS5((char*)"Models/Sala2/ps5/source/PS5/PS5.obj");
    Model XboxLogo((char*)"Models/Sala2/XboxLogo/Screenshot_2025_11_07_1108045114_texture.obj");
    Model NswitchLogo((char*)"Models/Sala2/NintendoLogo/_1108052044_texture.obj");
    Model PS5Logo((char*)"Models/Sala2/PlayStationLogo/PS_1108045851_texture.obj");

    //Sala3 
    Model game((char*)"Models/sala3/Game_ready_3D_prop_a_1110045024_texture.obj");
    Model vr((char*)"Models/sala3/VR_headset_with_two_m_1105231651_texture.obj");
    Model warrior((char*)"Models/sala3/Animation_Walking_withSkin.fbx");
    Model console((char*)"Models/sala3/Game_ready_3D_prop_a_1110065502_texture.obj");



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
    // (MUEVE SOLO ESTOS VALORES)
    // ===============================

    // Headset VR
    glm::vec3 VR_HEADSET_POS = glm::vec3(-32.0f, 4.0f, -10.0f);
    float     VR_HEADSET_YAW = 360.0f;   // grados sobre Y
    float     VR_HEADSET_SCL = 15.0f;

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

        // Escenario
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(4.0f, FLOOR_Y - LIFT, -16.0f));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            escenario.Draw(lightingShader);
        }

        // Son los modelos de la sala 1

        // Maquina de arcade roja
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


      
        // ====== PEDESTAL (posición separada) ======
        {
            quadShader.Use();
            GLint qModel = glGetUniformLocation(quadShader.Program, "model");
            GLint qView = glGetUniformLocation(quadShader.Program, "view");
            GLint qProj = glGetUniformLocation(quadShader.Program, "projection");
            GLint qTex = glGetUniformLocation(quadShader.Program, "uTex");
            GLint qTile = glGetUniformLocation(quadShader.Program, "uTiling");
            glUniformMatrix4fv(qView, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(qProj, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(qTex, 0);
            glUniform2f(qTile, 2.0f, 2.0f);

            glm::mat4 m(1.0f);
            m = glm::translate(m, PEDESTAL_POS);
            m = glm::rotate(m, glm::radians(PEDESTAL_YAW), glm::vec3(0, 1, 0));
            m = glm::scale(m, PEDESTAL_SCALE);
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texPedestal);
            glBindVertexArray(pedVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // ====== FLECHAS SOBRE EL PISO (posición separada) ======
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            quadShader.Use();
            GLint qModel = glGetUniformLocation(quadShader.Program, "model");
            GLint qView = glGetUniformLocation(quadShader.Program, "view");
            GLint qProj = glGetUniformLocation(quadShader.Program, "projection");
            GLint qTex = glGetUniformLocation(quadShader.Program, "uTex");
            GLint qTile = glGetUniformLocation(quadShader.Program, "uTiling");
            glUniformMatrix4fv(qView, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(qProj, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(qTex, 0);
            glUniform2f(qTile, ARROWS_TILING.x, ARROWS_TILING.y);

            glm::mat4 m(1.0f);
            m = glm::translate(m, ARROWS_POS);
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            m = glm::rotate(m, glm::radians(ARROWS_YAW), glm::vec3(0, 0, 1));
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texArrows);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);

            glDisable(GL_BLEND);
        }

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



// Cubo base 1
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT, 3.0f));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase1.Draw(lightingShader);
        }
        // Cubo base 2
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT, 15.0f));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase2.Draw(lightingShader);
        }
        // Cubo base 3
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT, 26.0f));
            m = glm::scale(m, glm::vec3(0.8f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            CuboBase3.Draw(lightingShader);
        }
        // Xbox SX
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.30f, 3.0f));
            m = glm::scale(m, glm::vec3(0.5f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            xboxSX.Draw(lightingShader);
        }
        // Switch
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 1.10f, 15.0f));
            m = glm::scale(m, glm::vec3(0.5f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            Nswitch.Draw(lightingShader);
        }
        // PS5
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(5.0f, FLOOR_Y + LIFT + 0.90f, 26.0f));
            m = glm::scale(m, glm::vec3(0.5f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            PS5.Draw(lightingShader);
        }


        // XboxLogo
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 3.0f));
            m = glm::scale(m, glm::vec3(1.5f));
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            XboxLogo.Draw(lightingShader);
        }
        // NswitchLogo
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 15.0f));
            m = glm::scale(m, glm::vec3(1.5f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            NswitchLogo.Draw(lightingShader);
        }
        // PS5Logo
        {
            glm::mat4 m(1);
            m = glm::translate(m, glm::vec3(2.20f, FLOOR_Y + LIFT + 3.5f, 26.0f));
            m = glm::scale(m, glm::vec3(1.5f));
            m = glm::rotate(m, glm::radians(90.0f), glm::vec3(0, 1, 0));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            PS5Logo.Draw(lightingShader);
        }

        //termina sala 2

        // ====== BACKPLATE ======
        {
            colorShader.Use();
            GLint cModel = glGetUniformLocation(colorShader.Program, "model");
            GLint cView = glGetUniformLocation(colorShader.Program, "view");
            GLint cProj = glGetUniformLocation(colorShader.Program, "projection");
            GLint cColor = glGetUniformLocation(colorShader.Program, "uColor");
            glUniformMatrix4fv(cView, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(cProj, 1, GL_FALSE, glm::value_ptr(projection));

            glUniform3f(cColor, 0.05f, 0.05f, 0.05f);
            glm::mat4 back(1.0f);
            back = glm::translate(back, BACK_POS);
            back = glm::rotate(back, glm::radians(BACK_YAW), glm::vec3(0, 1, 0));
            back = glm::scale(back, BACK_SCALE);
            glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(back));
            glBindVertexArray(lampVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
        }

        // ====== GLOW ======
        {
            colorShader.Use();
            GLint cModel = glGetUniformLocation(colorShader.Program, "model");
            GLint cView = glGetUniformLocation(colorShader.Program, "view");
            GLint cProj = glGetUniformLocation(colorShader.Program, "projection");
            GLint cColor = glGetUniformLocation(colorShader.Program, "uColor");
            glUniformMatrix4fv(cView, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(cProj, 1, GL_FALSE, glm::value_ptr(projection));

            glUniform3f(cColor, 0.85f, 0.20f, 0.95f);
            glm::mat4 glow(1.0f);
            glow = glm::translate(glow, GLOW_POS);
            glow = glm::rotate(glow, glm::radians(GLOW_YAW), glm::vec3(0, 1, 0));
            glow = glm::scale(glow, GLOW_SCALE);
            glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(glow));
            glBindVertexArray(lampVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
        }

        // ====== RÓTULO “VR ESTACIÓN” ======
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            quadShader.Use();
            GLint qModel = glGetUniformLocation(quadShader.Program, "model");
            GLint qView = glGetUniformLocation(quadShader.Program, "view");
            GLint qProj = glGetUniformLocation(quadShader.Program, "projection");
            GLint qTex = glGetUniformLocation(quadShader.Program, "uTex");
            GLint qTile = glGetUniformLocation(quadShader.Program, "uTiling");
            glUniformMatrix4fv(qView, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(qProj, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform1i(qTex, 0);
            glUniform2f(qTile, 1.0f, 1.0f);

            // empuja el rótulo hacia su "forward" para despegarlo del muro
            glm::vec3 forward = ForwardFromYaw(SIGN_YAW);
            glm::mat4 m(1.0f);
            m = glm::translate(m, SIGN_POS + forward * SIGN_Z_BIAS);
            m = glm::rotate(m, glm::radians(SIGN_YAW), glm::vec3(0, 1, 0));
            // flip opcional en X si lo ves espejeado
            float sx = SIGN_FLIP_X ? -SIGN_SIZE.x : SIGN_SIZE.x;
            m = glm::scale(m, glm::vec3(sx, SIGN_SIZE.y, 1.0f));
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texSign);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);

            glDisable(GL_BLEND);
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
            m = glm::translate(m, glm::vec3(-25.0f, FLOOR_Y + LIFT, 2.5f));
            m = glm::rotate(m, glm::radians(45.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(0.02f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            warrior.Draw(skinnedShader);
        }

        // ====== game (prop de sala) — CORREGIDO translate ======
        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-20.0f, FLOOR_Y + LIFT, 2.5f)); 
            m = glm::rotate(m, glm::radians(VR_HEADSET_YAW), glm::vec3(0, 1, 0)); 
            m = glm::scale(m, glm::vec3(2.5f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m)); 
            game.Draw(lightingShader); 
        }

        {
            lightingShader.Use(); GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model"); glm::mat4 m(1.0f); // *** FIX: pasar matriz base y vec3; usar FLOOR_Y + LIFT para asentar en el piso 
            m = glm::translate(m, glm::vec3(-20.0f, 5.0f , -13.0f)); 
            m = glm::rotate(m, glm::radians(360.0f), glm::vec3(0, 1, 0)); 
            m = glm::scale(m, glm::vec3(2.0f)); glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m)); 
            console.Draw(lightingShader); 
        }

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
void KeyCallback(GLFWwindow* window, int key, int, int action, int) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
    if (key >= 0 && key < 1024) { if (action == GLFW_PRESS) keys[key] = true; else if (action == GLFW_RELEASE) keys[key] = false; }
}
void Animation() { if (AnimBall) rotBall += 0.04f; if (AnimDog) rotDog -= 0.006f; }
void MouseCallback(GLFWwindow*, double x, double y) {
    if (firstMouse) { lastX = (float)x; lastY = (float)y; firstMouse = false; }
    float xo = (float)x - lastX, yo = lastY - (float)y; lastX = (float)x; lastY = (float)y; camera.ProcessMouseMovement(xo, yo);
}
