#pragma once

#include <vk_types.h>
#include <unordered_map>
#include <filesystem>

#include "Mesh.h"
#include "Image.h"
#include "Buffer.h"
#include "Sampler.h"
#include "vk_descriptors.h"

#define DRAW_INDIRECT 1
#define GPU_CULLING 1

//forward declaration
class VulkanEngine;
class CommandBuffer;
class ComputePipeline;

struct GLTFMetallic_Roughness {
    std::unique_ptr<GraphicsPipeline> opaquePipeline;
    std::unique_ptr<GraphicsPipeline> transparentPipeline;

    struct alignas (16) MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec2 metal_rough_factors;
        uint32_t colourID;
    };

    struct MaterialResources {
        size_t colourID;
        Sampler* colorSampler;
        size_t metalRoughnessID;
        Sampler* metalRoughSampler;
        Buffer* dataBuffer;
    };

    DescriptorWriter writer;

    void build_pipelines(VkDescriptorSetLayout textureLayout);
    void clear_resources();

    MaterialInstance write_material(MaterialPass pass, const MaterialResources& resources, uint32_t id);
};

struct RenderObject {
    VkDrawIndexedIndirectCommand drawCmd;

    MaterialInstance* material;

    Mesh::Bounds bounds;
    glm::mat4 transform;
};

struct DrawContext {
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
};

struct ObjectData {
    alignas (16) glm::mat4 modelMatrix;
    glm::uint matID;
};

struct InstanceData {
    glm::uint transformID;
    glm::uint surfaceID;
};

struct SurfaceData {
    alignas (16) glm::vec3 origin;
    glm::uint indexCount;
    alignas (16) glm::vec3 extents;
    glm::uint firstIndex;
    glm::uint matID;
    glm::uint isTransparent;
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

struct LoadedGLTF {

    // storage for all the data on a given glTF file
    std::unordered_map<std::string, std::shared_ptr<Mesh>> meshes;
    std::unordered_map<std::string, std::shared_ptr<Node>> nodes;
    std::vector<std::shared_ptr<Image>> images;
    std::unordered_map<std::string, std::shared_ptr<Mesh::GLTFMaterial>> materials;

    std::shared_ptr<Buffer> m_indexBuffer;
    std::shared_ptr<Buffer> m_vertexBuffer;

    std::vector<uint32_t> m_opaqueDraws;

    // nodes that dont have a parent, for iterating through the file in tree order
    std::vector<std::shared_ptr<Node>> topNodes;

    std::vector<Sampler> samplers;

    uint32_t m_objectCount = 0;

    VkDescriptorSetLayout m_textureLayout;
    VkDescriptorSet		  m_textureSet;

    GLTFMetallic_Roughness metalRoughMaterial;

    DescriptorAllocatorGrowable descriptorPool;

    std::unique_ptr<Buffer> m_instanceBuffer;
    std::unique_ptr<Buffer> m_transformBuffer;
    std::unique_ptr<Buffer> m_surfaceBuffer;
    std::unique_ptr<Buffer> m_materialDataBuffer;

    ~LoadedGLTF() { clearAll(); };

    void createPipelines();
    void createBuffers(std::span<uint32_t> indices, std::span<Vertex> vertices);
    void updateContext(const glm::mat4& topMatrix);
    void cull(CommandBuffer& cmd, glm::mat4 frustum, Buffer& objectDataBuffer, Buffer& drawsBuffer, ComputePipeline *cullpipeline);
    void draw(CommandBuffer& cmd, VkDescriptorSet globalDescriptor, glm::mat4 frustum, Buffer& objectDataBuffer, Buffer& drawsBuffer);

    DrawContext& getContext() { return m_drawContext; }

private:

    DrawContext m_drawContext;

    void clearAll();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(std::string_view filePath);
