#pragma once

#include "VK/vk_types.h"

#include "SceneGraph.h"

std::optional<std::shared_ptr<SceneGraph>> loadGltf(std::string_view filePath);
