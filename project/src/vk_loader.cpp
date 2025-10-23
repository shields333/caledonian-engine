#include <vk_loader.h>

#include <iostream>

#include "vk_mem_alloc.h"

#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include "Mesh.h"
#include "ComputePipeline.h"

#include <glm/gtx/quaternion.hpp>

bool is_visible(const RenderObject& obj, const glm::mat4& viewproj) {
    std::array<glm::vec3, 8> corners{
        glm::vec3 { 1, 1, 1 },
        glm::vec3 { 1, 1, -1 },
        glm::vec3 { 1, -1, 1 },
        glm::vec3 { 1, -1, -1 },
        glm::vec3 { -1, 1, 1 },
        glm::vec3 { -1, 1, -1 },
        glm::vec3 { -1, -1, 1 },
        glm::vec3 { -1, -1, -1 },
    };

    glm::mat4 matrix = viewproj * obj.transform;

    glm::vec3 min = { 1.5, 1.5, 1.5 };
    glm::vec3 max = { -1.5, -1.5, -1.5 };

    for (int c = 0; c < 8; c++) {
        // project each corner into clip space
        glm::vec4 v = matrix * glm::vec4(obj.bounds.origin + (corners[c] * obj.bounds.extents), 1.f);

        // perspective correction
        v.x = v.x / v.w;
        v.y = v.y / v.w;
        v.z = v.z / v.w;

        min = glm::min(glm::vec3{ v.x, v.y, v.z }, min);
        max = glm::max(glm::vec3{ v.x, v.y, v.z }, max);
    }

    // check the clip space box is within the view
    if (min.z > 1.f || max.z < 0.f || min.x > 1.f || max.x < -1.f || min.y > 1.f || max.y < -1.f) {
        return false;
    }
    else {
        return true;
    }
}

void MeshNode::Draw(const glm::mat4& topMatrix, DrawContext& ctx)
{
    glm::mat4 nodeMatrix = topMatrix * worldTransform;

    for (auto& s : mesh->getSurfaces()) {
        RenderObject def;
        def.drawCmd = s.indirectCommand;
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = nodeMatrix;

        if (s.material->data.passType == MaterialPass::Transparent)
            ctx.TransparentSurfaces.push_back(def);
        else
            ctx.OpaqueSurfaces.push_back(def);
    }

    // recurse down
    Node::Draw(topMatrix, ctx);
}

void LoadedGLTF::createPipelines()
{
    // create texture array descriptor layout
    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.clear();
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, UINT16_MAX);
    m_textureLayout = layoutBuilder.build(VulkanEngine::getDevice()->getHandle(), VK_SHADER_STAGE_FRAGMENT_BIT);

    metalRoughMaterial.build_pipelines(m_textureLayout);
}

void LoadedGLTF::createBuffers(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
    m_textureSet = descriptorPool.allocate(m_textureLayout);

    //write the texture array to its descriptor set
    DescriptorWriter writer;
    writer.writeImageArray(0, images, VulkanEngine::get().m_defaultSamplerLinear->getHandle(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
    writer.update_set(VulkanEngine::getDevice()->getHandle(), m_textureSet);

    // shouldn't overwrite an existing mesh
    assert(!m_indexBuffer);

    const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
    const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

    //create vertex buffer
    m_vertexBuffer = std::make_shared<Buffer>(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    //create index buffer
    m_indexBuffer = std::make_shared<Buffer>(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    Buffer staging(vertexBufferSize + indexBufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_ONLY);

    void* data;
    vmaMapMemory(VulkanEngine::get().m_allocator, staging.getAlloacation(), &data);

    // copy vertex buffer
    memcpy(data, vertices.data(), vertexBufferSize);
    // copy index buffer
    memcpy((char*)data + vertexBufferSize, indices.data(), indexBufferSize);

    VulkanEngine::get().immediateSubmit([&](VkCommandBuffer cmd) {
        VkBufferCopy vertexCopy{ 0 };
        vertexCopy.dstOffset = 0;
        vertexCopy.srcOffset = 0;
        vertexCopy.size = vertexBufferSize;

        vkCmdCopyBuffer(cmd, staging.getHandle(), m_vertexBuffer->getHandle(), 1, &vertexCopy);

        VkBufferCopy indexCopy{ 0 };
        indexCopy.dstOffset = 0;
        indexCopy.srcOffset = vertexBufferSize;
        indexCopy.size = indexBufferSize;

        vkCmdCopyBuffer(cmd, staging.getHandle(), m_indexBuffer->getHandle(), 1, &indexCopy);
        });

    vmaUnmapMemory(VulkanEngine::get().m_allocator, staging.getAlloacation());

    for (auto& mesh : meshes)
        mesh.second->setBuffers(m_vertexBuffer, m_indexBuffer);
    
}

void LoadedGLTF::updateContext(const glm::mat4& topMatrix)
{
    m_drawContext.OpaqueSurfaces.clear();
    m_drawContext.TransparentSurfaces.clear();
    m_opaqueDraws.clear();

    // create renderables from the scenenodes
    for (auto& n : topNodes) {
        n->Draw(topMatrix, m_drawContext);
    }
}

void LoadedGLTF::cull(CommandBuffer& cmd, glm::mat4 frustum, Buffer& objectDataBuffer, Buffer& drawsBuffer, ComputePipeline* cullpipeline)
{
    size_t opaqueDrawSize = m_drawContext.OpaqueSurfaces.size() * sizeof(VkDrawIndexedIndirectCommand) + sizeof(uint32_t);
    size_t opaqueObjSize = m_drawContext.OpaqueSurfaces.size() * sizeof(ObjectData);

    uint32_t* opaqueDrawPtr = (uint32_t*)drawsBuffer.getInfo().pMappedData;
    uint32_t* transparentDrawPtr = (uint32_t*)((char*)opaqueDrawPtr + opaqueDrawSize);

#if GPU_CULLING
    //add counters for draws
    VulkanEngine::get().m_stats.drawcall_count = opaqueDrawPtr[0] + transparentDrawPtr[0];

    // reset draw counts to 0
    memset(opaqueDrawPtr, 0, 4);
    memset(transparentDrawPtr, 0, 4);

    CullPushConstants pushConstants;
    pushConstants.viewproj = frustum;
    pushConstants.objCount = m_objectCount;
    pushConstants.inInstanceBuf = m_instanceBuffer->getAddress();
    pushConstants.inTransformBuf = m_transformBuffer->getAddress();
    pushConstants.inSurfaceBuf = m_surfaceBuffer->getAddress();
    pushConstants.outOpaqueObjBuf = objectDataBuffer.getAddress();
    pushConstants.outTransparentObjBuf = objectDataBuffer.getAddress() + opaqueObjSize;
    pushConstants.outOpaqueDrawBuf = drawsBuffer.getAddress();
    pushConstants.outTransparentDrawBuf = drawsBuffer.getAddress() + opaqueDrawSize;

    cmd.bindPipeline(cullpipeline->getHandle(), VK_PIPELINE_BIND_POINT_COMPUTE);

    cmd.pushConstants(&pushConstants, sizeof(CullPushConstants), cullpipeline->getLayout(), VK_SHADER_STAGE_COMPUTE_BIT);

    cmd.barrier(objectDataBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);
    cmd.barrier(drawsBuffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT);

    uint32_t groupCount = m_objectCount / 32 + (m_objectCount % 32 ? 1 : 0);
    cmd.dispatch(groupCount, 1, 1);

    cmd.barrier(objectDataBuffer, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_READ_BIT);
    cmd.barrier(drawsBuffer, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);

#else
    m_opaqueDraws.reserve(m_drawContext.OpaqueSurfaces.size());

    // frustum cull
    for (uint32_t i = 0; i < m_drawContext.OpaqueSurfaces.size(); i++) {
        if (is_visible(m_drawContext.OpaqueSurfaces[i], frustum))
            m_opaqueDraws.push_back(i);
    }

    // sort the opaque surfaces by material and mesh
    std::sort(m_opaqueDraws.begin(), m_opaqueDraws.end(), [&](const auto& iA, const auto& iB) {
        const RenderObject& A = m_drawContext.OpaqueSurfaces[iA];
        const RenderObject& B = m_drawContext.OpaqueSurfaces[iB];
        return A.material < B.material;
        });

    //TODO: create map function to Buffer
    ObjectData* opaqueData = (ObjectData*)objectDataBuffer.getInfo().pMappedData;
    ObjectData* transparentData = opaqueData + m_drawContext.OpaqueSurfaces.size();

    *opaqueDrawPtr = m_opaqueDraws.size();
    VkDrawIndexedIndirectCommand* opaqueDrawCmds = (VkDrawIndexedIndirectCommand*)(opaqueDrawPtr + 1);

    *transparentDrawPtr = m_drawContext.TransparentSurfaces.size();
    VkDrawIndexedIndirectCommand* transparentDrawCmds = (VkDrawIndexedIndirectCommand*)(transparentDrawPtr + 1);

    // write per draw data into the buffer
    for (int i = 0; i < m_opaqueDraws.size(); i++) {
        opaqueData[i].modelMatrix = m_drawContext.OpaqueSurfaces[m_opaqueDraws[i]].transform;
        opaqueData[i].matID = m_drawContext.OpaqueSurfaces[m_opaqueDraws[i]].material->matID;
        m_drawContext.OpaqueSurfaces[m_opaqueDraws[i]].drawCmd.firstInstance = i;
        opaqueDrawCmds[i] = m_drawContext.OpaqueSurfaces[m_opaqueDraws[i]].drawCmd;
    }

    for (int i = 0; i < m_drawContext.TransparentSurfaces.size(); i++) {
        transparentData[i].matID = m_drawContext.TransparentSurfaces[i].material->matID;
        transparentData[i].modelMatrix = m_drawContext.TransparentSurfaces[i].transform;
        m_drawContext.TransparentSurfaces[i].drawCmd.firstInstance = i;
        transparentDrawCmds[i] = m_drawContext.TransparentSurfaces[i].drawCmd;
    }

    //add counters for draws
    VulkanEngine::get().m_stats.drawcall_count += m_opaqueDraws.size() + m_drawContext.TransparentSurfaces.size();
#endif
}

void LoadedGLTF::draw(CommandBuffer& cmd, VkDescriptorSet globalDescriptor, glm::mat4 frustum, Buffer& objectDataBuffer, Buffer& drawsBuffer)
{
    cmd.bindIndexBuffer(*m_indexBuffer);

#if DRAW_INDIRECT
    size_t opaqueDrawSize = m_drawContext.OpaqueSurfaces.size() * sizeof(VkDrawIndexedIndirectCommand) + sizeof(uint32_t);
    size_t opaqueObjSize = m_drawContext.OpaqueSurfaces.size() * sizeof(ObjectData);

    auto drawIndirect = [&](const RenderObject& draw, VkDeviceSize drawOffset, uint32_t drawCount, VkDeviceSize countOffset, VkDeviceAddress objectDataAddress) {
        // bind mesh pipeline
        cmd.bindPipeline(draw.material->pipeline->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS);

        // bind descriptor sets
        std::vector<VkDescriptorSet> sets = { globalDescriptor, m_textureSet };
        cmd.bindDescriptorSets(sets, draw.material->pipeline->getLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS);

        Mesh::GPUDrawPushConstants pushConstants;
        pushConstants.vertexBufferAddress = m_vertexBuffer->getAddress();
        pushConstants.objectBufferAddress = objectDataAddress;
        pushConstants.materialBufferAddress = m_materialDataBuffer->getAddress();
        cmd.pushConstants(&pushConstants, sizeof(Mesh::GPUDrawPushConstants), draw.material->pipeline->getLayout(), VkShaderStageFlagBits(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));

        cmd.drawIndirectCount(drawsBuffer, drawOffset, drawCount, sizeof(VkDrawIndexedIndirectCommand), drawsBuffer, countOffset);
        };

    if (!m_drawContext.OpaqueSurfaces.empty())
        drawIndirect(m_drawContext.OpaqueSurfaces[0], sizeof(uint32_t), m_drawContext.OpaqueSurfaces.size(), 0, objectDataBuffer.getAddress());

    if (!m_drawContext.TransparentSurfaces.empty())
        drawIndirect(m_drawContext.TransparentSurfaces[0], sizeof(uint32_t) + opaqueDrawSize, m_drawContext.TransparentSurfaces.size(), opaqueDrawSize, objectDataBuffer.getAddress() + opaqueObjSize);

#else
    //defined outside of the draw function, this is the state we will try to skip
    GraphicsPipeline* lastPipeline = nullptr;

    auto draw = [&](const RenderObject& draw) {
        if (lastPipeline != draw.material->pipeline) {
            // bind mesh pipeline
            cmd.bindPipeline(draw.material->pipeline->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS);

            std::vector<VkDescriptorSet> sets = { globalDescriptor, m_textureSet };

            cmd.bindDescriptorSets(sets, draw.material->pipeline->getLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS);

            lastPipeline = draw.material->pipeline;
        }

        Mesh::GPUDrawPushConstants pushConstants;
        pushConstants.vertexBufferAddress = m_vertexBuffer->getAddress();
        pushConstants.objectBufferAddress = objectDataBuffer.getAddress();
        pushConstants.materialBufferAddress = m_materialDataBuffer->getAddress();

        cmd.pushConstants(&pushConstants, sizeof(Mesh::GPUDrawPushConstants), draw.material->pipeline->getLayout(), VkShaderStageFlagBits(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT));

        cmd.drawIndexed(draw.drawCmd.indexCount, draw.drawCmd.instanceCount, draw.drawCmd.firstIndex, draw.drawCmd.vertexOffset, draw.drawCmd.firstInstance);
        };

    for (auto& r : m_opaqueDraws) {
        draw(m_drawContext.OpaqueSurfaces[r]);
    }

    for (auto& r : m_drawContext.TransparentSurfaces) {
        draw(r);
    }
#endif

}

void LoadedGLTF::clearAll()
{
    descriptorPool.destroy_pools(VulkanEngine::getDevice()->getHandle());
    m_materialDataBuffer.reset();

    metalRoughMaterial.clear_resources();
    vkDestroyDescriptorSetLayout(VulkanEngine::getDevice()->getHandle(), m_textureLayout, nullptr);

    meshes.clear();
    nodes.clear();
    images.clear();
    materials.clear();

    topNodes.clear();
    samplers.clear();
}

void GLTFMetallic_Roughness::build_pipelines(VkDescriptorSetLayout textureLayout)
{
    VkPushConstantRange matrixRange{};
    matrixRange.offset = 0;
    matrixRange.size = sizeof(Mesh::GPUDrawPushConstants);
    matrixRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    std::vector<VkPushConstantRange> pushConstants = { matrixRange };

    DescriptorLayoutBuilder layoutBuilder;
    layoutBuilder.add_binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

    std::vector<VkDescriptorSetLayout> layouts = { VulkanEngine::get().m_gpuSceneDataDescriptorLayout, textureLayout };

    // build the stage-create-info for both vertex and fragment stages. This lets
    // the pipeline know the shader modules per stage
    PipelineBuilder builder;

    builder.setShaders("../project/shaders/mesh.vert.spv", "../project/shaders/mesh.frag.spv");
    builder.createPipelineLayout(&layouts, &pushConstants);
    builder.setTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
    builder.setPolygonMode(VK_POLYGON_MODE_FILL);
    builder.setCullMode(VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE);

    builder.setColourAttachmentFormat(VulkanEngine::get().m_drawImage->getFormat());
    builder.setDepthFormat(VulkanEngine::get().m_depthImage->getFormat());
    builder.enableDepthtest(true, VK_COMPARE_OP_GREATER_OR_EQUAL);

    opaquePipeline.reset(builder.buildPipeline());

    builder.setShaders("../project/shaders/mesh.vert.spv", "../project/shaders/mesh.frag.spv");
    builder.createPipelineLayout(&layouts, &pushConstants); // TODO: creating duplicate layout to avoid validation warnings
    builder.enableBlendingAlphablend();
    builder.enableDepthtest(false, VK_COMPARE_OP_GREATER_OR_EQUAL);

    transparentPipeline.reset(builder.buildPipeline());
}

void GLTFMetallic_Roughness::clear_resources()
{
    opaquePipeline.reset();
    transparentPipeline.reset();
}

MaterialInstance GLTFMetallic_Roughness::write_material(MaterialPass pass, const MaterialResources& resources, uint32_t id)
{
    MaterialInstance matData;
    matData.passType = pass;
    if (pass == MaterialPass::Transparent) {
        matData.pipeline = transparentPipeline.get();
    }
    else {
        matData.pipeline = opaquePipeline.get();
    }

    matData.materialAddress = resources.dataBuffer->getAddress();
    matData.matID = id;

    return matData;
}
