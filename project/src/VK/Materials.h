#pragma once

#include "vk_types.h"
#include "vk_descriptors.h"

class GraphicsPipeline;
class Sampler;
class Buffer;

enum class MaterialPass :uint8_t {
    Opaque,
    Cutout,
    Transparent
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
        glm::vec4 colourFactors;
        glm::vec3 emmisiveFactors;
        float     normalScale;
        glm::vec2 metalRoughFactors;
        uint32_t colourID;
        uint32_t metalRoughnessID;
        uint32_t normalID;
        uint32_t emissiveID;
        float alphaCutout;
    };

    struct MaterialResources {
        size_t colourID;
        Sampler* colorSampler;

        size_t metalRoughnessID;
        Sampler* metalRoughSampler;

        size_t normalID;
        Sampler* normalSampler;

        size_t emissiveID;
        Sampler* emissiveSampler;

        Buffer* dataBuffer;
    };

    DescriptorWriter writer;

    void build_pipelines();
    void clear_resources();

    MaterialInstance write_material(MaterialPass pass, const MaterialResources& resources, uint32_t id);
};
