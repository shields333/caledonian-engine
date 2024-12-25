#pragma once

#include "vk_types.h"

class GraphicsPipeline
{
public:
    GraphicsPipeline(VkGraphicsPipelineCreateInfo &info);
    ~GraphicsPipeline();

    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;

    VkPipeline getHandle() { return m_handle; }
    VkPipelineLayout getLayout() { return m_layout; }

private:
    VkPipeline m_handle = VK_NULL_HANDLE;

    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

class PipelineBuilder 
{
public:
    PipelineBuilder() { setDefault(); }

    void setDefault();

    //void ConfigureInputState()
    void    setShaders(std::string vertPath, std::string fragPath);
    void    createPipelineLayout(std::vector<VkDescriptorSetLayout>* descriptorLayouts, std::vector<VkPushConstantRange>* pushConstants = nullptr);
    void    setTopology(VkPrimitiveTopology topology);
    void    setPolygonMode(VkPolygonMode mode);
    void    setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
    void    setColourAttachmentFormat(VkFormat format);
    void    setDepthFormat(VkFormat format);
    void    enableDepthtest(bool depthWriteEnable, VkCompareOp op);

    GraphicsPipeline* buildPipeline();

private:
    std::vector<VkPipelineShaderStageCreateInfo> m_shaderStages;

    VkPipelineInputAssemblyStateCreateInfo m_inputAssembly;
    VkPipelineRasterizationStateCreateInfo m_rasterizer;
    VkPipelineColorBlendAttachmentState m_colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo m_multisampling;
    VkPipelineLayout m_pipelineLayout;
    VkPipelineDepthStencilStateCreateInfo m_depthStencil;
    VkPipelineRenderingCreateInfo m_renderInfo;
    VkFormat m_colorAttachmentformat;

};