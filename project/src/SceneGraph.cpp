#include "SceneGraph.h"

#include "vk_engine.h"

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
        def.indexCount = s.count;
        def.firstIndex = s.startIndex;
        def.indexBuffer = &mesh->getIndexBuffer();
        def.material = &s.material->data;
        def.bounds = s.bounds;
        def.transform = nodeMatrix;
        def.vertexBufferAddress = mesh->getBufferAddress();

        ctx.OpaqueSurfaces.push_back(def);
    }

    // recurse down
    Node::Draw(topMatrix, ctx);
}

void SceneGraph::updateScene()
{
    m_mainContext.OpaqueSurfaces.clear();
    m_mainContext.TransparentSurfaces.clear();
}

void SceneGraph::drawScene(CommandBuffer& cmd, VkDescriptorSet globalDescriptor)
{
    std::vector<uint32_t> opaque_draws;
    opaque_draws.reserve(m_mainContext.OpaqueSurfaces.size());

    for (uint32_t i = 0; i < m_mainContext.OpaqueSurfaces.size(); i++) {
        if (is_visible(m_mainContext.OpaqueSurfaces[i], VulkanEngine::get().m_sceneData.viewproj))
            opaque_draws.push_back(i);
    }

    // sort the opaque surfaces by material and mesh
    std::sort(opaque_draws.begin(), opaque_draws.end(), [&](const auto& iA, const auto& iB) {
        const RenderObject& A = m_mainContext.OpaqueSurfaces[iA];
        const RenderObject& B = m_mainContext.OpaqueSurfaces[iB];
        if (A.material == B.material) {
            return A.indexBuffer < B.indexBuffer;
        }
        else {
            return A.material < B.material;
        }
        });

    //defined outside of the draw function, this is the state we will try to skip
    GraphicsPipeline* lastPipeline = nullptr;
    MaterialInstance* lastMaterial = nullptr;
    Buffer* lastIndexBuffer = nullptr;


    auto draw = [&](const RenderObject& draw) {

        if (lastMaterial != draw.material) {
            std::vector<VkDescriptorSet> sets = { globalDescriptor, draw.material->materialSet };

            if (lastPipeline != draw.material->pipeline) {
                // bind mesh pipeline
                cmd.bindPipeline(draw.material->pipeline->getHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS);
                lastPipeline = draw.material->pipeline;
            }
            cmd.bindDescriptorSets(sets, draw.material->pipeline->getLayout(), VK_PIPELINE_BIND_POINT_GRAPHICS);
            lastMaterial = draw.material;
        }

        if (lastIndexBuffer != draw.indexBuffer) {
            cmd.bindIndexBuffer(*draw.indexBuffer);
            lastIndexBuffer = draw.indexBuffer;
        }

        Mesh::GPUDrawPushConstants pushConstants;
        pushConstants.vertexBufferAddress = draw.vertexBufferAddress;
        pushConstants.worldMatrix = draw.transform;
        cmd.pushConstants(&pushConstants, sizeof(Mesh::GPUDrawPushConstants), draw.material->pipeline->getLayout(), VK_SHADER_STAGE_VERTEX_BIT);

        cmd.drawIndexed(draw.indexCount, 1, draw.firstIndex, 0, 0);
        };

    for (auto& r : opaque_draws) {
        draw(m_mainContext.OpaqueSurfaces[r]);
    }

    for (auto& r : m_mainContext.TransparentSurfaces) {
        draw(r);
    }

}
