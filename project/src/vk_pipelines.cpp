#include <vk_pipelines.h>

#include <fstream>
#include <map>

#include "vk_engine.h"

SpvReflectShaderModule getReflectModule(const void* data, size_t size)
{
    SpvReflectShaderModule module = {};
    SpvReflectResult result = spvReflectCreateShaderModule(size, data, &module);
    assert(result == SPV_REFLECT_RESULT_SUCCESS);

    return module;
}


bool vkutil::load_shader_module(const char* filePath,
    VkDevice device,
	VkPipelineShaderStageCreateInfo& outShaderModule,
    SpvReflectShaderModule* outReflectModule)
{
    // open the file. With cursor at the end
    std::ifstream file(filePath, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        return false;
    }

    // find what the size of the file is by looking up the location of the cursor
    // because the cursor is at the end, it gives the size directly in bytes
    size_t fileSize = (size_t)file.tellg();

    // spirv expects the buffer to be on uint32, so make sure to reserve a int
    // vector big enough for the entire file
    std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

    // put file cursor at beginning
    file.seekg(0);

    // load the entire file into the buffer
    file.read((char*)buffer.data(), fileSize);

    // now that the file is loaded into the buffer, we can close it
    file.close();

    // create a new shader module, using the buffer we loaded
    VkShaderModuleCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.pNext = nullptr;

    // codeSize has to be in bytes, so multply the ints in the buffer by size of
    // int to know the real size of the buffer
    createInfo.codeSize = buffer.size() * sizeof(uint32_t);
    createInfo.pCode = buffer.data();

    // check that the creation goes well.
    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        return false;
    }
    *outReflectModule = getReflectModule(buffer.data(), buffer.size() * sizeof(uint32_t));
	outShaderModule = vkinit::pipeline_shader_stage_create_info(static_cast<VkShaderStageFlagBits>(outReflectModule->shader_stage), shaderModule, outReflectModule->entry_point_name);

    return true;
}

VkPipelineLayout vkutil::createPiplineLayout(const SpvReflectShaderModule& reflectModule, std::vector<VkDescriptorSetLayout>& descriptorLayouts)
{
	std::vector<SpvReflectShaderModule> modules(1, reflectModule);
	return createPiplineLayout(modules, descriptorLayouts);
}

VkPipelineLayout vkutil::createPiplineLayout(const std::vector<SpvReflectShaderModule>& reflectModules, std::vector<VkDescriptorSetLayout>& descriptorLayouts)
{
	std::vector<VkPushConstantRange> pushConstRanges;

	std::map<uint8_t, std::map<uint8_t, VkDescriptorSetLayoutBinding>> layoutMap;
	std::map<uint16_t, VkPushConstantRange> pushConstantMap;

	for (auto& reflectModule : reflectModules) {
		uint32_t count = 0;
		SpvReflectResult result = spvReflectEnumerateDescriptorSets(&reflectModule, &count, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		std::vector<SpvReflectDescriptorSet*> sets(count);
		result = spvReflectEnumerateDescriptorSets(&reflectModule, &count, sets.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		// map the sets
		for (const auto set : sets) {
			// if the set has already been created then add to it
			if (layoutMap.contains(set->set)) {
				std::map<uint8_t, VkDescriptorSetLayoutBinding>& bindingMap = layoutMap.find(set->set)->second;

				for (uint32_t i_binding = 0; i_binding < set->binding_count; ++i_binding) {
					// if the binding is missing from the set then add a new one to the map
					// otherwise we just need to add the stage flag to the existing one
					auto binding = bindingMap.find(set->bindings[i_binding]->binding);
					if (binding == bindingMap.end()) {
						VkDescriptorSetLayoutBinding layoutBinding;
						const SpvReflectDescriptorBinding& reflBinding = *(set->bindings[i_binding]);
						layoutBinding.binding = reflBinding.binding;
						layoutBinding.descriptorType = static_cast<VkDescriptorType>(reflBinding.descriptor_type);
						layoutBinding.descriptorCount = 1;
						for (uint32_t i_dim = 0; i_dim < reflBinding.array.dims_count; ++i_dim) {
							layoutBinding.descriptorCount *= reflBinding.array.dims[i_dim];
						}
						layoutBinding.stageFlags = static_cast<VkShaderStageFlagBits>(reflectModule.shader_stage);
						bindingMap.insert(std::pair<uint8_t, VkDescriptorSetLayoutBinding>(reflBinding.binding, std::move(layoutBinding)));
					}
					else {
						binding->second.stageFlags |= reflectModule.shader_stage;
					}
				}

				continue;
			}
		
			// create a new set and its bindings
			std::map<uint8_t, VkDescriptorSetLayoutBinding>& bindingMap = layoutMap.emplace(set->set, std::map<uint8_t, VkDescriptorSetLayoutBinding>()).first->second;
			for (uint32_t i_binding = 0; i_binding < set->binding_count; ++i_binding) {
				const SpvReflectDescriptorBinding& reflBinding = *(set->bindings[i_binding]);
				VkDescriptorSetLayoutBinding layoutBinding = {};

				layoutBinding.binding = reflBinding.binding;
				layoutBinding.descriptorType = static_cast<VkDescriptorType>(reflBinding.descriptor_type);
				layoutBinding.descriptorCount = 1;

				// assume that an array of 1 is boundless
				if (reflBinding.array.dims_count == 1 && reflBinding.array.dims[0] == 1)
					layoutBinding.descriptorCount = UINT16_MAX;
				else
					for (uint32_t i_dim = 0; i_dim < reflBinding.array.dims_count; ++i_dim) {
						layoutBinding.descriptorCount *= reflBinding.array.dims[i_dim];
					}
				layoutBinding.stageFlags = static_cast<VkShaderStageFlagBits>(reflectModule.shader_stage);
				bindingMap.insert(std::pair<uint8_t, VkDescriptorSetLayoutBinding>(reflBinding.binding, std::move(layoutBinding)));
			}
		}

		result = spvReflectEnumeratePushConstantBlocks(&reflectModule, &count, NULL);
		assert(result == SPV_REFLECT_RESULT_SUCCESS);
		std::vector<SpvReflectBlockVariable*> pushConstants(count);
		result = spvReflectEnumeratePushConstantBlocks(&reflectModule, &count, pushConstants.data());
		assert(result == SPV_REFLECT_RESULT_SUCCESS);

		auto getKey = [] (uint16_t offset, uint16_t size){
			return (offset << 8) | size;
		};

		// map the push constant blocks
		for (auto pushConstant : pushConstants) {
			// if the push constant already exists then add the stage flag and continue
			if (pushConstantMap.contains(getKey(pushConstant->offset, pushConstant->size))) {
				pushConstantMap.find(getKey(pushConstant->offset, pushConstant->size))->second.stageFlags |= static_cast<VkShaderStageFlagBits>(reflectModule.shader_stage);

				continue;
			}

			// create a new push constant range
			VkPushConstantRange range = {};

			range.offset = pushConstant->offset;
			range.size = pushConstant->padded_size;
			range.stageFlags = static_cast<VkShaderStageFlagBits>(reflectModule.shader_stage);

			pushConstantMap.insert(std::pair<uint16_t, VkPushConstantRange>(getKey(pushConstant->offset, pushConstant->size), std::move(range)));
		}
	}

	descriptorLayouts.reserve(layoutMap.size());
	// create the VkDescriptorSetLayouts
	for (auto& layout : layoutMap) {
		std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
		layoutBindings.reserve(layout.second.size());

		// move the binding info from the map to a vector
		for (auto& binding : layout.second)
			layoutBindings.push_back(std::move(binding.second));

		VkDescriptorSetLayoutCreateInfo setLayoutInfo = {};
		setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setLayoutInfo.bindingCount = layoutBindings.size();
		setLayoutInfo.pBindings = layoutBindings.data();

		descriptorLayouts.emplace_back();
		VK_CHECK(vkCreateDescriptorSetLayout(VulkanEngine::getDevice()->getHandle(), &setLayoutInfo, nullptr, &descriptorLayouts.back()));
	}

	pushConstRanges.reserve(pushConstantMap.size());
	// fill the vector with the push constants
	for (auto& range : pushConstantMap) {
		pushConstRanges.push_back(std::move(range.second));
	}

	VkPipelineLayoutCreateInfo computeLayout{};
	computeLayout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	computeLayout.pNext = nullptr;

	if (!descriptorLayouts.empty()) {
		computeLayout.pSetLayouts = descriptorLayouts.data();
		computeLayout.setLayoutCount = descriptorLayouts.size();
	}
	// add push constants if supplied
	if (!pushConstRanges.empty()) {
		computeLayout.pPushConstantRanges = pushConstRanges.data();
		computeLayout.pushConstantRangeCount = pushConstRanges.size();
	}

	VkPipelineLayout pipelineLayout;
	VK_CHECK(vkCreatePipelineLayout(VulkanEngine::getDevice()->getHandle(), &computeLayout, nullptr, &pipelineLayout));

	return pipelineLayout;
}
