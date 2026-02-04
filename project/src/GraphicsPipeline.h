#pragma once

#include "vk_types.h"

class GraphicsPipeline
{
public:
    GraphicsPipeline(std::vector<std::string> shaderPaths, VkGraphicsPipelineCreateInfo &info);
    ~GraphicsPipeline();

    GraphicsPipeline(GraphicsPipeline&& other) noexcept;
    GraphicsPipeline& operator=(GraphicsPipeline&& other) noexcept;

    VkPipeline getHandle() { return m_handle; }
    VkPipelineLayout getLayout() { return m_layout; }
    VkDescriptorSetLayout getDescriptorLayout(int i) { return m_descriptorLayouts[i]; }
private:
    VkPipeline m_handle = VK_NULL_HANDLE;
    // TODO: the layout should be a pointer to a map
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayout> m_descriptorLayouts;
};

class PipelineBuilder 
{
public:
    PipelineBuilder() { setDefault(); }

    void setDefault();

    //void ConfigureInputState()
    void    setTopology(VkPrimitiveTopology topology);
    void    setPolygonMode(VkPolygonMode mode);
    void    setCullMode(VkCullModeFlags cullMode, VkFrontFace frontFace);
    void    setColourAttachmentFormat(VkFormat format);
    void    setDepthFormat(VkFormat format);
    void    enableDepthtest(bool depthWriteEnable, VkCompareOp op);
    void    enableBlendingAdditive();
    void    enableBlendingAlphablend();

    GraphicsPipeline* buildPipeline(std::vector<std::string> shaderPaths);

private:
    VkPipelineInputAssemblyStateCreateInfo m_inputAssembly;
    VkPipelineRasterizationStateCreateInfo m_rasterizer;
    VkPipelineColorBlendAttachmentState m_colorBlendAttachment;
    VkPipelineMultisampleStateCreateInfo m_multisampling;
    VkPipelineDepthStencilStateCreateInfo m_depthStencil;
    VkPipelineRenderingCreateInfo m_renderInfo;
    VkFormat m_colorAttachmentformat;

};