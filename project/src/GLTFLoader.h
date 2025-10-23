#pragma once

#include <vk_types.h>

#include "vk_loader.h"

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(std::string_view filePath);
