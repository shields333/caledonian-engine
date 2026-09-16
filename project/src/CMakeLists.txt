
set(main
  main.cpp
  camera.cpp
  camera.h
  GuiManager.cpp
  GuiManager.h
)

set(vk 
  VK/vk_engine.h
  VK/vk_engine.cpp
  VK/vk_types.h
  VK/vk_initializers.cpp
  VK/vk_initializers.h
  VK/vk_descriptors.h
  VK/vk_descriptors.cpp
  VK/vk_pipelines.h
  VK/vk_pipelines.cpp
  VK/Device.cpp
  VK/Device.h
  VK/Swapchain.cpp
  VK/Swapchain.h
  VK/CommandPool.cpp
  VK/CommandPool.h
  VK/FencePool.cpp
  VK/FencePool.h
  VK/Queue.cpp
  VK/Queue.h
  VK/Image.cpp
  VK/Image.h 
  VK/Sampler.cpp
  VK/Sampler.h
  VK/Buffer.cpp
  VK/Buffer.h
  VK/Mesh.cpp
  VK/Mesh.h
  VK/ComputePipeline.cpp
  VK/ComputePipeline.h
  VK/GraphicsPipeline.cpp
  VK/GraphicsPipeline.h
  VK/Materials.h
  VK/Materials.cpp
)

set(SceneGraph
  SceneGraph/SceneGraph.h
  SceneGraph/SceneGraph.cpp
  SceneGraph/GLTFLoader.cpp
  SceneGraph/GLTFLoader.h
)

source_group("\\" FILES ${main})
source_group("VK\\" FILES ${vk})
source_group("SceneGraph\\" FILES ${SceneGraph})

# Add source to this project's executable.
add_executable (engine 
  ${main}
  ${vk}
  ${SceneGraph}
)

set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_compile_definitions(engine PUBLIC GLM_FORCE_DEPTH_ZERO_TO_ONE)
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(engine PUBLIC vma glm Vulkan::Vulkan fmt::fmt stb_image SDL2::SDL2 vkbootstrap imgui fastgltf::fastgltf spirv-reflect mikktspace)

#target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp>  <glm/vec4.hpp> <vulkan/vulkan.h>)

add_custom_command(TARGET engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
  COMMAND_EXPAND_LISTS
  )