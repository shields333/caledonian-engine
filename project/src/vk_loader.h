#pragma once

#include <vk_types.h>
#include <unordered_map>
#include <filesystem>

#include "Mesh.h"
#include "Image.h"
#include "Buffer.h"
#include "Sampler.h"
#include "vk_descriptors.h"
#include "SceneGraph.h"

//forward declaration
class VulkanEngine;

struct LoadedGLTF : public IRenderable {

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

    virtual void Draw(const glm::mat4& topMatrix, DrawContext& ctx);

private:

    void clearAll();
};

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(std::string_view filePath);
