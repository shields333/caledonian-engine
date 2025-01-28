#pragma once

#include <vk_types.h>
#include <unordered_map>
#include <filesystem>

#include "Mesh.h"
#include "Image.h"
#include "Buffer.h"
#include "Sampler.h"
#include "vk_descriptors.h"

//forward declaration
class VulkanEngine;
class CommandBuffer;

struct RenderObject {
    uint32_t indexCount;
    uint32_t firstIndex;
    Buffer* indexBuffer;

    MaterialInstance* material;

    Mesh::Bounds bounds;
    glm::mat4 transform;
    VkDeviceAddress vertexBufferAddress;
};

struct DrawContext {
    std::vector<RenderObject> OpaqueSurfaces;
    std::vector<RenderObject> TransparentSurfaces;
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
    std::unordered_map<std::string, std::shared_ptr<Image>> images;
    std::unordered_map<std::string, std::shared_ptr<Mesh::GLTFMaterial>> materials;

    // nodes that dont have a parent, for iterating through the file in tree order
    std::vector<std::shared_ptr<Node>> topNodes;

    std::vector<Sampler> samplers;

    DescriptorAllocatorGrowable descriptorPool;

    std::unique_ptr<Buffer> materialDataBuffer;

    ~LoadedGLTF() { clearAll(); };

    void updateContext(const glm::mat4& topMatrix);
    void draw(CommandBuffer& cmd, VkDescriptorSet globalDescriptor, glm::mat4 frustum);

    DrawContext& getContext() { return m_drawContext; }

private:

    DrawContext m_drawContext;

    void clearAll();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(std::string_view filePath);
