#pragma once
#include <string>
#include <vector>
#include <GL/glew.h>
#include <glm/glm.hpp>
#include "Shader.h"
#include <assimp/scene.h>

struct Vertex {
    glm::vec3 Position;
    glm::vec3 Normal;
    glm::vec2 TexCoords;
};

#define MAX_BONE_INFLUENCE 4
struct VertexBoneData {
    int   IDs[MAX_BONE_INFLUENCE] = { 0,0,0,0 };
    float Weights[MAX_BONE_INFLUENCE] = { 0,0,0,0 };
    void AddBoneData(int id, float w) {
        for (int i = 0; i < MAX_BONE_INFLUENCE; i++) {
            if (Weights[i] == 0.0f) { IDs[i] = id; Weights[i] = w; return; }
        }
    }
};

struct Texture {
    GLuint id{};
    std::string type;
    aiString path;
};

class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<GLuint> indices;
    std::vector<Texture> textures;
    std::vector<VertexBoneData> bones;

    Mesh(const std::vector<Vertex>& v,
        const std::vector<GLuint>& idx,
        const std::vector<Texture>& tex,
        const std::vector<VertexBoneData>& b = {})
        : vertices(v), indices(idx), textures(tex), bones(b) {
        setupMesh();
    }

    void Draw(Shader& shader) {
        GLuint diffuseNr = 1, specularNr = 1;
        for (GLuint i = 0; i < textures.size(); ++i) {
            glActiveTexture(GL_TEXTURE0 + i);
            std::string name = textures[i].type;
            std::string number = (name == "texture_diffuse")
                ? std::to_string(diffuseNr++)
                : std::to_string(specularNr++);
            glUniform1i(glGetUniformLocation(shader.Program, (name + number).c_str()), i);
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, (GLsizei)indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        for (GLuint i = 0; i < textures.size(); ++i) { glActiveTexture(GL_TEXTURE0 + i); glBindTexture(GL_TEXTURE_2D, 0); }
    }

private:
    GLuint VAO = 0, VBO = 0, EBO = 0, VBO_bones = 0;

    void setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), indices.data(), GL_STATIC_DRAW);

        // position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Position));
        // normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
        // texcoords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

        if (!bones.empty()) {
            glGenBuffers(1, &VBO_bones);
            glBindBuffer(GL_ARRAY_BUFFER, VBO_bones);
            glBufferData(GL_ARRAY_BUFFER, bones.size() * sizeof(VertexBoneData), bones.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(5);
            glVertexAttribIPointer(5, 4, GL_INT, sizeof(VertexBoneData), (void*)offsetof(VertexBoneData, IDs));
            glEnableVertexAttribArray(6);
            glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(VertexBoneData), (void*)offsetof(VertexBoneData, Weights));
        }
        glBindVertexArray(0);
    }
};
