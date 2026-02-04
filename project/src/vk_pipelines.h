#pragma once 
#include <vk_types.h>

#include <vk_pipelines.h>
#include <vk_initializers.h>
#include <spirv_reflect.h>

namespace vkutil {

    bool load_shader_module(const char* filePath,
        VkDevice device,
        VkPipelineShaderStageCreateInfo& outShaderModule,
        SpvReflectShaderModule* outReflectModule);

    VkPipelineLayout createPiplineLayout(const SpvReflectShaderModule& reflectModule, std::vector<VkDescriptorSetLayout>& descriptorLayouts);
    VkPipelineLayout createPiplineLayout(const std::vector<SpvReflectShaderModule>& reflectModules, std::vector<VkDescriptorSetLayout>& descriptorLayouts);

};