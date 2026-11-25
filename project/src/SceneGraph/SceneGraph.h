#pragma once

#include <unordered_map>
#include <filesystem>

#include "VK/vk_types.h"
#include "VK/Mesh.h"
#include "VK/Image.h"
#include "VK/Buffer.h"
#include "VK/Sampler.h"
#include "VK/vk_descriptors.h"
#include "VK/Materials.h"

#define DRAW_INDIRECT 1

#if DRAW_INDIRECT
#define GPU_CULLING
#endif

//forward declaration
class VulkanEngine;
class CommandBuffer;
class ComputePipeline;
class GraphicsPipeline;

struct RenderObject {
    VkDrawIndexedIndirectCommand drawCmd;

    MaterialInstance* material;

    Mesh::Bounds bounds;
    glm::mat4 transform;
};

struct DrawContext {
    std::vector<RenderObject> opaqueSurfaces;
    std::vector<RenderObject> cutoutSurfaces;
    std::vector<RenderObject> transparentSurfaces;
};

struct ObjectData {
    alignas (16) glm::mat4 modelMatrix;
    glm::uint matID;
};

struct InstanceData {
    glm::uint transformID;
    glm::uint surfaceID;
    alignas (4) bool visible;
};

struct SurfaceData {
    alignas (16) glm::vec3 origin;
    float radius;
    glm::uint indexCount;
    glm::uint firstIndex;
    glm::uint matID;
    glm::uint bufferOffset;
};

// base class for a renderable dynamic object
class IRenderable {
    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) = 0;
};

// implementation of a drawable scene node.
// the scene node can hold children and will also keep a transform to propagate
// to them
struct Node : public IRenderable {

    // parent pointer must be a weak pointer to avoid circular dependencies
    std::weak_ptr<Node> parent;
    std::vector<std::shared_ptr<Node>> children;

    glm::mat4 localTransform;
    glm::mat4 worldTransform;

    void refreshTransform(const glm::mat4& parentMatrix)
    {
        worldTransform = parentMatrix * localTransform;
        for (auto c : children) {
            c->refreshTransform(worldTransform);
        }
    }

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx)
    {
        // draw children
        for (auto& c : children) {
            c->Draw(topMatrix, ctx);
        }
    }
};

struct MeshNode : public Node {

    std::shared_ptr<Mesh> mesh;

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx) override;
};

struct SceneGraph {

    // storage for all the data on a given glTF file
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::vector<std::shared_ptr<Image>> images;
    std::unordered_map<std::string, std::shared_ptr<MaterialInstance>> materials;

    std::shared_ptr<Buffer> m_indexBuffer;
    std::shared_ptr<Buffer> m_vertexBuffer;

    std::vector<uint32_t> m_opaqueDraws;

    // nodes that dont have a parent, for iterating through the file in tree order
    std::vector<std::shared_ptr<Node>> topNodes;

    std::vector<Sampler> samplers;

    uint32_t m_objectCount = 0;

    VkDescriptorSetLayout m_textureLayout;
    VkDescriptorSetLayout m_gpuSceneDataDescriptorLayout;

    VkDescriptorSet		  m_textureSet;

    GLTFMetallic_Roughness m_metalRoughMaterial;

    DescriptorAllocatorGrowable m_descriptorPool;

    std::unique_ptr<Buffer> m_instanceBuffer;
    std::unique_ptr<Buffer> m_transformBuffer;
    std::unique_ptr<Buffer> m_surfaceBuffer;
    std::unique_ptr<Buffer> m_materialDataBuffer;

    ~SceneGraph() { clearAll(); };

    void createPipelines();
    void createBuffers(std::span<uint32_t> indices, std::span<Vertex> vertices);
    void updateContext(const glm::mat4& topMatrix);
    void cull(CommandBuffer& cmd, Buffer& objectDataBuffer, Buffer& drawsBuffer, ComputePipeline *cullpipeline, VkDescriptorSet descriptorSet, uint32_t pass);
    void draw(CommandBuffer& cmd, VkDescriptorSet globalDescriptor, glm::mat4 frustum, Buffer& objectDataBuffer, Buffer& drawsBuffer);

    DrawContext& getContext() { return m_drawContext; }

private:

    DrawContext m_drawContext;

    void clearAll();
};