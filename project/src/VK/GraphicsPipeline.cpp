#include "GraphicsPipeline.h"

#include "vk_engine.h"
#include <spirv_reflect.h>

#include "vk_initializers.h"
#include "vk_pipelines.h"


GraphicsPipeline::GraphicsPipeline(std::vector<std::string> shaderPaths, VkGraphicsPipelineCreateInfo &info)
{
    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfo;
    std::vector<SpvReflectShaderModule> reflModules;
    shaderStageInfo.reserve(shaderPaths.size());
    reflModules.reserve(shaderPaths.size());

    //layout code
    for (auto path : shaderPaths) {
        shaderStageInfo.emplace_back();
        SpvReflectShaderModule reflectModule;
        if (!vkutil::load_shader_module(path.c_str(), VulkanEngine::getDevice()->getHandle(), shaderStageInfo.back(), &reflectModule))
        {
            fmt::print("Error when building the shader\n");
        }
        reflModules.push_back(reflectModule);

    }

    info.layout = m_layout = vkutil::createPiplineLayout(reflModules, m_descriptorLayouts);
    info.stageCount = shaderStageInfo.size();
    info.pStages = shaderStageInfo.data();

    VK_CHECK(vkCreateGraphicsPipelines(VulkanEngine::getDevice()->getHandle(), VK_NULL_HANDLE, 1, &info,
        nullptr, &m_handle));

    for (int i = 0; i < shaderPaths.size(); i++) {
        vkDestroyShaderModule(VulkanEngine::getDevice()->getHandle(), shaderStageInfo[i].module, nullptr);
        spvReflectDestroyShaderModule(&reflModules[i]);
    }
}

GraphicsPipeline::~GraphicsPipeline()
{
    vkDestroyPipelineLayout(VulkanEngine::getDevice()->getHandle(), m_layout, nullptr);
    vkDestroyPipeline(VulkanEngine::getDevice()->getHandle(), m_handle, nullptr);
    for (auto& descLayout : m_descriptorLayouts)
        vkDestroyDescriptorSetLayout(VulkanEngine::getDevice()->getHandle(), descLayout, nullptr);
}

GraphicsPipeline::GraphicsPipeline(GraphicsPipeline&& other) noexcept
{
    *this = std::move(other);
}

GraphicsPipeline& GraphicsPipeline::operator=(GraphicsPipeline&& other) noexcept
{
    m_layout = std::exchange(other.m_layout, VK_NULL_HANDLE);
    m_handle = std::exchange(other.m_handle, VK_NULL_HANDLE);
    m_descriptorLayouts.swap(other.m_descriptorLayouts);
    return *this;
}

void PipelineBuilder::setDefault()
{
    // clear all of the structs to their default values

    m_inputAssembly = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    m_inputAssembly.primitiveRestartEnable = VK_FALSE;
    m_inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    m_rasterizer = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    m_rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    m_rasterizer.lineWidth = 1.f;
    // back face culling
    m_rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;

    m_colorBlendAttachment = {};
    m_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    // no blending
    m_colorBlendAttachment.blendEnable = VK_FALSE;

    m_multisampling = { .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    m_multisampling.sampleShadingEnable = VK_FALSE;
    // multisampling defaulted to no multisampling (1 sample per pixel)
    m_multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    m_multisampling.minSampleShading = 1.0f;
    m_multisampling.pSampleMask = nullptr;
    // no alpha to coverage either
    m_multisampling.alphaToCoverageEnable = VK_FALSE;
    m_multisampling.alphaToOneEnable = VK_FALSE;

    m_depthStencil = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };

    m_renderInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
}

void PipelineBuilder::setTopology(VkPrimitiveTopology topology)
{
    m_inputAssembly.topology = topology;
}

void PipelineBuilder::setPolygonMode(VkPolygonMode mode)
{
    m_rasterizer.polygonMode = mode;
}

void PipelineBuilder::setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace)
{
    m_rasterizer.cullMode = cullMode;
    m_rasterizer.frontFace = frontFace;
}

void PipelineBuilder::setColourAttachmentFormat(VkFormat format)
{
    m_colorAttachmentformat = format;
    // connect the format to the renderInfo  structure
    m_renderInfo.colorAttachmentCount = 1;
    m_renderInfo.pColorAttachmentFormats = &m_colorAttachmentformat;
}

void PipelineBuilder::setDepthFormat(VkFormat format)
{
    m_renderInfo.depthAttachmentFormat = format;
}

void PipelineBuilder::enableDepthtest(bool depthWriteEnable, VkCompareOp op)
{
    m_depthStencil.depthTestEnable = VK_TRUE;
    m_depthStencil.depthWriteEnable = depthWriteEnable;
    m_depthStencil.depthCompareOp = op;
    m_depthStencil.depthBoundsTestEnable = VK_FALSE;
    m_depthStencil.stencilTestEnable = VK_FALSE;
    m_depthStencil.front = {};
    m_depthStencil.back = {};
    m_depthStencil.minDepthBounds = 0.f;
    m_depthStencil.maxDepthBounds = 1.f;
}

void PipelineBuilder::enableBlendingAdditive()
{
    m_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_colorBlendAttachment.blendEnable = VK_TRUE;
    m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
    m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

void PipelineBuilder::enableBlendingAlphablend()
{
    m_colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    m_colorBlendAttachment.blendEnable = VK_TRUE;
    m_colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    m_colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    m_colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    m_colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    m_colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    m_colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
}

GraphicsPipeline* PipelineBuilder::buildPipeline(std::vector<std::string> shaderPaths)
{
    // make viewport state from our stored viewport and scissor.
    // at the moment we wont support multiple viewports or scissors
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.pNext = nullptr;

    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // setup dummy color blending. We arent using transparent objects yet
    // the blending is just "no blend", but we do write to the color attachment
    VkPipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.pNext = nullptr;

    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &m_colorBlendAttachment;

    // completely clear VertexInputStateCreateInfo, as we have no need for it
    VkPipelineVertexInputStateCreateInfo _vertexInputInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };

    // build the actual pipeline
    // we now use all of the info structs we have been writing into into this one
    // to create the pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo = { .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    // connect the renderInfo to the pNext extension mechanism
    pipelineInfo.pNext = &m_renderInfo;

    pipelineInfo.pVertexInputState = &_vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &m_inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &m_rasterizer;
    pipelineInfo.pMultisampleState = &m_multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDepthStencilState = &m_depthStencil;

    VkDynamicState state[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };

    VkPipelineDynamicStateCreateInfo dynamicInfo = { .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dynamicInfo.pDynamicStates = &state[0];
    dynamicInfo.dynamicStateCount = 2;

    pipelineInfo.pDynamicState = &dynamicInfo;
    
    auto pipeline = new GraphicsPipeline(shaderPaths, pipelineInfo);

    return pipeline;
}

