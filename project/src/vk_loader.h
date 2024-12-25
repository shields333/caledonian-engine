#pragma once

#include <vk_types.h>
#include <unordered_map>
#include <filesystem>


//forward declaration
class VulkanEngine;
class Mesh;

std::optional<std::vector<std::shared_ptr<Mesh>>> loadGltfMeshes(VulkanEngine* engine, std::filesystem::path filePath);