#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cstdlib>         // atoi
#include <GL/glew.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include "SOIL2/SOIL2.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/config.h>

#include "Mesh.h"
#include "Shader.h"

// ---- Compatibilidad con versiones antiguas de Assimp ----
#if !defined(aiTextureType_BASE_COLOR)
#define HAS_BASE_COLOR 0
#else
#define HAS_BASE_COLOR 1
#endif

static inline const aiTexture* GetEmbeddedTextureCompat(const aiScene* sc, const aiString& str) {
    if (str.length > 0 && str.C_Str()[0] == '*') {
        int idx = std::atoi(str.C_Str() + 1);
        if (idx >= 0 && idx < (int)sc->mNumTextures) return sc->mTextures[idx];
    }
    return nullptr;
}

static inline glm::mat4 AiToGlm(const aiMatrix4x4& m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4);
}

struct BoneInfo { glm::mat4 offset{ 1.0f }; glm::mat4 finalTransform{ 1.0f }; };

GLint TextureFromFile(const char* path, std::string directory) {
    std::string filename = std::string(path);
    filename = directory + '/' + filename;
    GLuint id; glGenTextures(1, &id);
    int w, h, ch;
    unsigned char* img = SOIL_load_image(filename.c_str(), &w, &h, &ch, SOIL_LOAD_AUTO);
    if (!img) { std::cout << "SOIL fail: " << filename << "\n"; return 0; }
    GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, img);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    SOIL_free_image_data(img);
    return id;
}

// Textura embebida en memoria (*0,*1…) o BGRA crudo
static GLuint TextureFromEmbedded(const aiTexture* tex) {
    if (!tex) return 0;
    int w = 0, h = 0, ch = 0;
    if (tex->mHeight == 0) {
        const unsigned char* data = reinterpret_cast<const unsigned char*>(tex->pcData);
        int len = tex->mWidth;
        unsigned char* img = SOIL_load_image_from_memory(data, len, &w, &h, &ch, SOIL_LOAD_AUTO);
        if (!img) return 0;
        GLuint id; glGenTextures(1, &id);
        GLenum fmt = (ch == 4) ? GL_RGBA : GL_RGB;
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, img);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        SOIL_free_image_data(img);
        return id;
    }
    else {
        w = tex->mWidth; h = tex->mHeight;
        const unsigned char* data = reinterpret_cast<const unsigned char*>(tex->pcData);
        GLuint id; glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_BGRA, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
        return id;
    }
}

class Model {
public:
    Model(const char* path) { loadModel(path); }
    void Draw(Shader& shader) { for (auto& m : meshes) m.Draw(shader); }

    void UpdateAnimation(double t) {
        if (!scene || scene->mNumAnimations == 0) return;
        const aiAnimation* a = scene->mAnimations[0];
        double tps = (a->mTicksPerSecond != 0.0) ? a->mTicksPerSecond : 25.0;
        double ticks = fmod(t * tps, a->mDuration);
        ReadNodeHierarchy(ticks, scene->mRootNode, glm::mat4(1.0f), a);
    }
    void GetBoneMatrices(std::vector<glm::mat4>& out, size_t maxBones = 100) const {
        out.assign(maxBones, glm::mat4(1.0f));
        for (size_t i = 0; i < m_BoneInfo.size() && i < maxBones; i++) out[i] = m_BoneInfo[i].finalTransform;
    }

private:
    std::vector<Mesh> meshes;
    std::vector<Texture> textures_loaded;
    std::string directory;

    Assimp::Importer importer;
    const aiScene* scene = nullptr;
    std::unordered_map<std::string, int> m_BoneMapping;
    std::vector<BoneInfo> m_BoneInfo;
    glm::mat4 m_GlobalInverseTransform{ 1.0f };

    void loadModel(const std::string& path) {
        unsigned flags =
            aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_LimitBoneWeights |
            aiProcess_JoinIdenticalVertices | aiProcess_ImproveCacheLocality | aiProcess_SortByPType |
            aiProcess_CalcTangentSpace | aiProcess_GenUVCoords | aiProcess_ValidateDataStructure | aiProcess_FlipUVs;

#ifdef AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS, true);
#endif
#ifdef AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_GEOMETRY_LAYERS, true);
#endif
#ifdef AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ALL_MATERIALS, true);
#endif
#ifdef AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS
        importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_PRESERVE_PIVOTS, false);
#endif

        scene = importer.ReadFile(path, flags);
        if (!scene || !scene->mRootNode) {
            std::cout << "ASSIMP ERROR: " << importer.GetErrorString() << "\n"; return;
        }
        directory = path.substr(0, path.find_last_of('/'));
        m_GlobalInverseTransform = glm::inverse(AiToGlm(scene->mRootNode->mTransformation));
        processNode(scene->mRootNode, scene);
    }

    void processNode(aiNode* node, const aiScene* sc) {
        for (unsigned i = 0; i < node->mNumMeshes; i++) {
            aiMesh* m = sc->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(m, sc));
        }
        for (unsigned i = 0; i < node->mNumChildren; i++) processNode(node->mChildren[i], sc);
    }

    int GetBoneIndex(const std::string& name) {
        auto it = m_BoneMapping.find(name);
        if (it != m_BoneMapping.end()) return it->second;
        int idx = (int)m_BoneInfo.size();
        m_BoneMapping[name] = idx; m_BoneInfo.push_back(BoneInfo{});
        return idx;
    }

    const aiNodeAnim* FindNodeAnim(const aiAnimation* a, const std::string& node) {
        for (unsigned i = 0; i < a->mNumChannels; i++) {
            const aiNodeAnim* ch = a->mChannels[i];
            if (node == ch->mNodeName.C_Str()) return ch;
        }
        return nullptr;
    }
    static unsigned FindKey(double t, unsigned n, const aiVectorKey* k) { for (unsigned i = 0; i < n - 1; i++) if (t < k[i + 1].mTime) return i; return n - 1; }
    static unsigned FindKey(double t, unsigned n, const aiQuatKey* k) { for (unsigned i = 0; i < n - 1; i++) if (t < k[i + 1].mTime) return i; return n - 1; }

    glm::vec3 InterpPos(const aiNodeAnim* ch, double t) {
        if (ch->mNumPositionKeys == 1) { auto& v = ch->mPositionKeys[0].mValue; return { v.x,v.y,v.z }; }
        unsigned i = FindKey(t, ch->mNumPositionKeys, ch->mPositionKeys), j = i + 1;
        double dt = ch->mPositionKeys[j].mTime - ch->mPositionKeys[i].mTime; float f = dt > 0 ? float((t - ch->mPositionKeys[i].mTime) / dt) : 0.f;
        aiVector3D a = ch->mPositionKeys[i].mValue, b = ch->mPositionKeys[j].mValue; aiVector3D r = a + (b - a) * f; return { r.x,r.y,r.z };
    }
    glm::quat InterpRot(const aiNodeAnim* ch, double t) {
        if (ch->mNumRotationKeys == 1) { auto q = ch->mRotationKeys[0].mValue; return { q.w,q.x,q.y,q.z }; }
        unsigned i = FindKey(t, ch->mNumRotationKeys, ch->mRotationKeys), j = i + 1;
        double dt = ch->mRotationKeys[j].mTime - ch->mRotationKeys[i].mTime; float f = dt > 0 ? float((t - ch->mRotationKeys[i].mTime) / dt) : 0.f;
        aiQuaternion qa = ch->mRotationKeys[i].mValue, qb = ch->mRotationKeys[j].mValue, qr; aiQuaternion::Interpolate(qr, qa, qb, f); qr.Normalize();
        return { qr.w,qr.x,qr.y,qr.z };
    }
    glm::vec3 InterpScale(const aiNodeAnim* ch, double t) {
        if (ch->mNumScalingKeys == 1) { auto& v = ch->mScalingKeys[0].mValue; return { v.x,v.y,v.z }; }
        unsigned i = FindKey(t, ch->mNumScalingKeys, ch->mScalingKeys), j = i + 1;
        double dt = ch->mScalingKeys[j].mTime - ch->mScalingKeys[i].mTime; float f = dt > 0 ? float((t - ch->mScalingKeys[i].mTime) / dt) : 0.f;
        aiVector3D a = ch->mScalingKeys[i].mValue, b = ch->mScalingKeys[j].mValue; aiVector3D r = a + (b - a) * f; return { r.x,r.y,r.z };
    }

    void ReadNodeHierarchy(double t, const aiNode* node, const glm::mat4& parent, const aiAnimation* a) {
        std::string name(node->mName.C_Str());
        glm::mat4 nodeT = AiToGlm(node->mTransformation);
        if (const aiNodeAnim* ch = FindNodeAnim(a, name)) {
            nodeT = glm::translate(glm::mat4(1), InterpPos(ch, t)) * glm::mat4_cast(InterpRot(ch, t)) * glm::scale(glm::mat4(1), InterpScale(ch, t));
        }
        glm::mat4 global = parent * nodeT;
        auto it = m_BoneMapping.find(name);
        if (it != m_BoneMapping.end()) {
            int idx = it->second;
            m_BoneInfo[idx].finalTransform = m_GlobalInverseTransform * global * m_BoneInfo[idx].offset;
        }
        for (unsigned i = 0; i < node->mNumChildren; i++) ReadNodeHierarchy(t, node->mChildren[i], global, a);
    }

    Mesh processMesh(aiMesh* mesh, const aiScene* sc) {
        std::vector<Vertex> verts; verts.reserve(mesh->mNumVertices);
        std::vector<GLuint> idx;
        std::vector<Texture> tex;
        std::vector<VertexBoneData> bonesData; bonesData.resize(mesh->mNumVertices);

        for (unsigned i = 0; i < mesh->mNumVertices; i++) {
            Vertex v{};
            v.Position = { mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z };
            v.Normal = { mesh->mNormals[i].x,  mesh->mNormals[i].y,  mesh->mNormals[i].z };
            if (mesh->mTextureCoords[0]) v.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
            else                         v.TexCoords = { 0.f,0.f };
            verts.push_back(v);
        }
        for (unsigned i = 0; i < mesh->mNumFaces; i++) {
            const aiFace& f = mesh->mFaces[i];
            for (unsigned j = 0; j < f.mNumIndices; j++) idx.push_back(f.mIndices[j]);
        }

        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = sc->mMaterials[mesh->mMaterialIndex];
            auto addTex = [&](aiTextureType t, const char* name) {
                auto list = loadMaterialTextures(material, t, name);
                tex.insert(tex.end(), list.begin(), list.end());
                };
            // Diffuse clásico
            addTex(aiTextureType_DIFFUSE, "texture_diffuse");
            // BASE_COLOR solo si existe en esta versión
#if HAS_BASE_COLOR
            addTex(aiTextureType_BASE_COLOR, "texture_diffuse");
#endif
            // Specular (opcional)
            addTex(aiTextureType_SPECULAR, "texture_specular");
        }

        if (mesh->mNumBones > 0) {
            for (unsigned b = 0; b < mesh->mNumBones; b++) {
                aiBone* ab = mesh->mBones[b];
                int boneIndex = GetBoneIndex(ab->mName.C_Str());
                m_BoneInfo[boneIndex].offset = AiToGlm(ab->mOffsetMatrix);
                for (unsigned w = 0; w < ab->mNumWeights; w++) {
                    unsigned vId = ab->mWeights[w].mVertexId;
                    float weight = ab->mWeights[w].mWeight;
                    if (vId < bonesData.size()) bonesData[vId].AddBoneData(boneIndex, weight);
                }
            }
        }
        return Mesh(verts, idx, tex, bonesData);
    }

    std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, std::string typeName) {
        std::vector<Texture> textures;
        unsigned count = mat->GetTextureCount(type);
        for (unsigned i = 0; i < count; i++) {
            aiString str; mat->GetTexture(type, i, &str);
            bool skip = false;
            for (auto& t : textures_loaded) { if (t.path == str) { textures.push_back(t); skip = true; break; } }
            if (skip) continue;

            Texture texture{}; texture.type = typeName; texture.path = str;

            // Detectar embebidas en versiones antiguas (*0,*1...)
            if (const aiTexture* emb = GetEmbeddedTextureCompat(scene, str)) {
                texture.id = TextureFromEmbedded(emb);
            }
            else {
                texture.id = TextureFromFile(str.C_Str(), directory);
            }

            if (texture.id) { textures.push_back(texture); textures_loaded.push_back(texture); }
        }
        return textures;
    }
};
