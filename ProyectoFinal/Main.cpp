// Proyecto final. Galería de videojuegos
// Integrantes:
// Perez Ortiz Sofia
// //Reynoso Ortega Francisco Javier
// No. de cuenta: 319074806
// Fecha de entrega: 19 de noviembre de 2025

#include <iostream>
#include <cmath>
#include <cstring>
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
const float FLOOR_Y = 0.0f; // el piso en Y=0
const float LIFT = 0.02f; // leve elevación para evitar z-fighting

// Cubo para pedestal/lámpara
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

// Quad 1x1 (XY) para rótulo/flechas/piso/pared
float quadData[] = {
    // pos              // normal   // uv
    -0.5f,-0.5f,0.0f,   0,0,1,     0,0,
     0.5f,-0.5f,0.0f,   0,0,1,     1,0,
     0.5f, 0.5f,0.0f,   0,0,1,     1,1,

     0.5f, 0.5f,0.0f,   0,0,1,     1,1,
    -0.5f, 0.5f,0.0f,   0,0,1,     0,1,
    -0.5f,-0.5f,0.0f,   0,0,1,     0,0
};

// anim/delta
float rotBall = 0.0f; bool AnimBall = false; bool AnimDog = false; float rotDog = 0.0f;
int dogAnim = 0; float FLegs = 0, RLegs = 0, head = 0, tail = 0; glm::vec3 dogPos(0); float dogRot = 0; bool step = false; float limite = 2.2f;
GLfloat deltaTime = 0.0f, lastFrame = 0.0f;

// VAOs/VBOs
GLuint lampVBO = 0, lampVAO = 0;
GLuint quadVAO = 0, quadVBO = 0;

// Texturas (Models/sala3/)
GLuint texSign = 0, texArrows = 0, texWoodFloor = 0, texWall = 0;

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
    glDisable(GL_BLEND);

    // Escribir shaders embebidos
    WriteTextFile("Shader/_skin_runtime.vs", SKIN_VS_SRC);
    WriteTextFile("Shader/_tex_runtime.frag", TEX_FRAG_SRC);
    WriteTextFile("Shader/_color_runtime.vs", COLOR_VS_SRC);
    WriteTextFile("Shader/_color_runtime.frag", COLOR_FRAG_SRC);
    WriteTextFile("Shader/_quad_runtime.vs", QUAD_VS_SRC);
    WriteTextFile("Shader/_quad_runtime.frag", QUAD_FRAG_SRC);

    Shader lightingShader("Shader/lighting.vs", "Shader/lighting.frag");
    Shader lampShader("Shader/lamp.vs", "Shader/lamp.frag");
    Shader skinnedShader("Shader/_skin_runtime.vs", "Shader/_tex_runtime.frag");
    Shader colorShader("Shader/_color_runtime.vs", "Shader/_color_runtime.frag");
    Shader quadShader("Shader/_quad_runtime.vs", "Shader/_quad_runtime.frag");

    // Modelos
    Model arc1((char*)"Models/arcade_machine.obj");
    Model arc2((char*)"Models/game_machine_0000001.obj");
    Model arc3((char*)"Models/Super_Famicom_Console_1105070442_texture.obj");
    Model arc4((char*)"Models/GameBoy_1105065316_texture.obj");
    Model arc5((char*)"Models/Atari_Console_Classic_1105064245_texture.obj");
    Model vr((char*)"Models/sala3/VR_headset_with_two_m_1105231651_texture.obj");
    Model warrior((char*)"Models/sala3/Animation_Walking_withSkin.fbx");

    // VAO cubo
    glGenVertexArrays(1, &lampVAO);
    glGenBuffers(1, &lampVBO);
    glBindVertexArray(lampVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lampVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);                   glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));   glEnableVertexAttribArray(1);
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

    // Samplers del lighting.frag
    lightingShader.Use();
    glUniform1i(glGetUniformLocation(lightingShader.Program, "material.diffuse"), 0);
    glUniform1i(glGetUniformLocation(lightingShader.Program, "material.specular"), 1);
    glUniform1f(glGetUniformLocation(lightingShader.Program, "material.shininess"), 16.0f);
    glUniform1i(glGetUniformLocation(lightingShader.Program, "transparency"), 0);

    // Sampler del skinned
    skinnedShader.Use();
    glUniform1i(glGetUniformLocation(skinnedShader.Program, "texture_diffuse1"), 0);

    // Cargar texturas
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    texSign = SOIL_load_OGL_texture("Models/sala3/vr_sign.png", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texArrows = SOIL_load_OGL_texture("Models/sala3/floor_arrows.png", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texWoodFloor = SOIL_load_OGL_texture("Models/sala3/wood_floor.jpg", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);
    texWall = SOIL_load_OGL_texture("Models/sala3/wall_concrete.jpg", SOIL_LOAD_AUTO, SOIL_CREATE_NEW_ID, SOIL_FLAG_MIPMAPS | SOIL_FLAG_INVERT_Y | SOIL_FLAG_COMPRESS_TO_DXT);

    if (!texSign)      std::cout << "No se cargo Models/sala3/vr_sign.png\n";
    if (!texArrows)    std::cout << "No se cargo Models/sala3/floor_arrows.png\n";
    if (!texWoodFloor) std::cout << "No se cargo Models/sala3/wood_floor.jpg\n";
    if (!texWall)      std::cout << "No se cargo Models/sala3/wall_concrete.jpg\n";

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
    glBindTexture(GL_TEXTURE_2D, 0);

    static double t0 = glfwGetTime();
    while (!glfwWindowShouldClose(window)) {
        float currentFrame = (float)glfwGetTime(); deltaTime = currentFrame - lastFrame; lastFrame = currentFrame;
        glfwPollEvents(); DoMovement(); Animation();

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glm::mat4 projection = glm::perspective(glm::radians(camera.GetZoom()), (float)SCREEN_WIDTH / (float)SCREEN_HEIGHT, 0.5f, 50.0f);
        glm::mat4 view = camera.GetViewMatrix();

        // ====== PISO 4x (tiling alto) ======
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
            glUniform2f(qTile, 16.0f, 16.0f); // más repetición por tamaño grande

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(0.0f, FLOOR_Y, -2.0f));
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            m = glm::scale(m, glm::vec3(40.0f, 32.0f, 1.0f)); // 4x (antes 10x8)
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texWoodFloor);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);

            glDisable(GL_BLEND);
        }

        // ====== MODELOS SOBRE EL PISO ======
        lightingShader.Use();
        GLint modelLoc = glGetUniformLocation(lightingShader.Program, "model");
        GLint viewLoc = glGetUniformLocation(lightingShader.Program, "view");
        GLint projLoc = glGetUniformLocation(lightingShader.Program, "projection");
        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        {
            glm::mat4 m(1);
            m = glm::translate(m, { -3.0f, FLOOR_Y + LIFT, -3.0f });
            m = glm::rotate(m, glm::radians(25.0f), { 0,1,0 });
            m = glm::scale(m, { 1.5f,1.5f,1.5f });
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc1.Draw(lightingShader);
        }
        {
            glm::mat4 m(1);
            m = glm::translate(m, { 3.0f, FLOOR_Y + LIFT, -3.0f });
            m = glm::rotate(m, glm::radians(-25.0f), { 0,1,0 });
            m = glm::scale(m, { 0.09f,0.09f,0.09f });
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc2.Draw(lightingShader);
        }
        {
            glm::mat4 m(1);
            m = glm::translate(m, { -5.6f, FLOOR_Y + LIFT, 1.0f });
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc3.Draw(lightingShader);
        }
        {
            glm::mat4 m(1);
            m = glm::translate(m, { -3.9f, FLOOR_Y + LIFT, 1.0f });
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc4.Draw(lightingShader);
        }
        {
            glm::mat4 m(1);
            m = glm::translate(m, { -2.3f, FLOOR_Y + LIFT, 1.0f });
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(m));
            arc5.Draw(lightingShader);
        }

        // ====== PEDESTAL (apoya en el piso) ======
        {
            colorShader.Use();
            GLint cModel = glGetUniformLocation(colorShader.Program, "model");
            GLint cView = glGetUniformLocation(colorShader.Program, "view");
            GLint cProj = glGetUniformLocation(colorShader.Program, "projection");
            GLint cColor = glGetUniformLocation(colorShader.Program, "uColor");
            glUniformMatrix4fv(cView, 1, GL_FALSE, glm::value_ptr(view));
            glUniformMatrix4fv(cProj, 1, GL_FALSE, glm::value_ptr(projection));
            glUniform3f(cColor, 0.12f, 0.12f, 0.12f);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(0.0f, 0.3f, -2.0f)); // base toca Y=0
            m = glm::scale(m, glm::vec3(0.6f, 0.6f, 0.6f));
            glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(m));

            glBindVertexArray(lampVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            glBindVertexArray(0);
        }

        // ====== FLECHAS ENCIMA DEL PISO ======
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

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(0.0f, FLOOR_Y + 0.001f, -2.0f));
            m = glm::rotate(m, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            m = glm::scale(m, glm::vec3(1.6f, 0.9f, 1.0f));
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texArrows);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);

            glDisable(GL_BLEND);
        }

        // ====== VR SOBRE EL PEDESTAL ======
        {
            lightingShader.Use();
            GLint modelLocVR = glGetUniformLocation(lightingShader.Program, "model");
            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(0.0f, 0.65f, -2.0f));
            m = glm::rotate(m, glm::radians(180.0f), glm::vec3(0, 1, 0));
            m = glm::scale(m, glm::vec3(22.0f)); // escala solicitada
            glUniformMatrix4fv(modelLocVR, 1, GL_FALSE, glm::value_ptr(m));
            vr.Draw(lightingShader);
        }

        // ====== PLACA (backplate + glow) ======
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
            back = glm::translate(back, glm::vec3(0.0f, 1.40f, -2.10f));
            back = glm::scale(back, glm::vec3(1.20f, 0.28f, 0.05f));
            glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(back));
            glBindVertexArray(lampVAO);
            glDrawArrays(GL_TRIANGLES, 0, 36);

            glUniform3f(cColor, 0.85f, 0.20f, 0.95f);
            glm::mat4 glow(1.0f);
            glow = glm::translate(glow, glm::vec3(0.0f, 1.40f, -2.03f));
            glow = glm::scale(glow, glm::vec3(1.10f, 0.20f, 0.02f));
            glUniformMatrix4fv(cModel, 1, GL_FALSE, glm::value_ptr(glow));
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

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(0.0f, 1.40f, -2.01f));
            m = glm::scale(m, glm::vec3(1.00f, 0.18f, 1.0f));
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texSign);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);

            glDisable(GL_BLEND);
        }

        // ====== PARED DE FONDO (tiling) ======
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
            glUniform2f(qTile, 3.0f, 1.5f);

            glm::mat4 m(1.0f);
            m = glm::translate(m, glm::vec3(0.0f, 1.4f, -2.6f)); // detrás del backplate
            m = glm::scale(m, glm::vec3(7.0f, 3.0f, 1.0f));
            glUniformMatrix4fv(qModel, 1, GL_FALSE, glm::value_ptr(m));

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texWall);
            glBindVertexArray(quadVAO);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindVertexArray(0);
            glBindTexture(GL_TEXTURE_2D, 0);
        }

        // ====== Guerrero skinned (sobre el piso) ======
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
            m = glm::translate(m, glm::vec3(0.0f, FLOOR_Y + LIFT, 2.5f));
            m = glm::scale(m, glm::vec3(0.01f));
            glUniformMatrix4fv(sModelLoc, 1, GL_FALSE, glm::value_ptr(m));
            warrior.Draw(skinnedShader);
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
