#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"

class GraphicsPipeline;
class Sampler;
class Buffer;

enum class MaterialPass :uint8_t {
    Opaque,
    Transparent,
    cutout
};

struct MaterialInstance {
    GraphicsPipeline* pipeline;
    VkDeviceAddress materialAddress;
    MaterialPass passType;
    uint32_t matID;
};

struct GLTFMetallic_Roughness {
    std::unique_ptr<GraphicsPipeline> opaquePipeline;
    std::unique_ptr<GraphicsPipeline> cutoutPipeline;
    std::unique_ptr<GraphicsPipeline> transparentPipeline;

    struct alignas (16) MaterialConstants {
        glm::vec4 colorFactors;
        glm::vec2 metal_rough_factors;
        uint32_t colourID;
        float alphaCutout;
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
