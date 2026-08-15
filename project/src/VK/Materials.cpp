#include "Materials.h"
#include "vk_engine.h"

void GLTFMetallic_Roughness::build_pipelines()
{
    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(Mesh::GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkPushConstantRange> pushConstants = { matrixRange };

    // build the stage-create-info for both vertex and fragment stages. This lets
    // the pipeline know the shader modules per stage
    PipelineBuilder builder;

    std::vector<std::string> shaderPaths = { "../project/shaders/mesh.vert.spv", "../project/shaders/mesh.frag.spv" };
    builder.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.setPolygonMode(VK_POLYGON_MODE_FILL);
    builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

    builder.setColourAttachmentFormat(VulkanEngine::get().m_drawImage->getFormat());
    builder.setDepthFormat(VulkanEngine::get().m_depthImage->getFormat());
    builder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    opaquePipeline.reset(builder.buildPipeline(shaderPaths));

    shaderPaths = { "../project/shaders/mesh.vert.spv", "../project/shaders/litCutout.frag.spv" };
    cutoutPipeline.reset(builder.buildPipeline(shaderPaths));

    shaderPaths = { "../project/shaders/mesh.vert.spv", "../project/shaders/mesh.frag.spv" };
    builder.enableBlendingAlphablend();
    builder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.reset(builder.buildPipeline(shaderPaths));
}

void GLTFMetallic_Roughness::clear_resources()
{
    opaquePipeline.reset();
    transparentPipeline.reset();
    cutoutPipeline.reset();
}

MaterialInstance GLTFMetallic_Roughness::write_material(MaterialPass pass, const MaterialResources& resources, uint32_t id)
{
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent) {
        matData.pipeline = transparentPipeline.get();
    }
    else if (pass == MaterialPass::Cutout){
        matData.pipeline = cutoutPipeline.get();
    }
    else {
        matData.pipeline = opaquePipeline.get();
    }

    matData.materialAddress = resources.dataBuffer->getAddress();
    matData.matID = id;

    return matData;
}
