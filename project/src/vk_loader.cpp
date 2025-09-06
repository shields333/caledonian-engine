#include <vk_loader.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <iostream>

#include "vk_mem_alloc.h"

#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include "Mesh.h"
#include "ComputePipeline.h"

#include <glm/gtx/quaternion.hpp>

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>


VkFilter extract_filter(fastgltf::Filter filter)
{
    switch (filter) {
        // nearest samplers
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;

        // linear samplers
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;

    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}

std::optional<std::shared_ptr<Image>> load_image(fastgltf::Asset& asset, fastgltf::Image& image)
{
    int width, height, nrChannels;

    std::shared_ptr<Image> newImage;

    std::visit(
        fastgltf::visitor{
            [](auto& arg) {},
            [&](fastgltf::sources::URI& filePath) {
                assert(filePath.fileByteOffset == 0); // We don't support offsets with stbi.
                assert(filePath.uri.isLocalPath()); // We're only capable of loading
                // local files.

                const std::string path(filePath.uri.path().begin(),
                    filePath.uri.path().end()); // Thanks C++.
                unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
                if (data) {
                    VkExtent3D imagesize;
                    imagesize.width = width;
                    imagesize.height = height;
                    imagesize.depth = 1;

                    newImage = std::make_shared<Image>(VK_FORMAT_R8G8B8A8_UNORM, imagesize, VK_IMAGE_USAGE_SAMPLED_BIT, data, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::Vector& vector) {
                unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()),
                    &width, &height, &nrChannels, 4);
                if (data) {
                    VkExtent3D imagesize;
                    imagesize.width = width;
                    imagesize.height = height;
                    imagesize.depth = 1;

                    newImage = std::make_shared<Image>(VK_FORMAT_R8G8B8A8_UNORM, imagesize, VK_IMAGE_USAGE_SAMPLED_BIT, data, true);

                    stbi_image_free(data);
                }
            },
            [&](fastgltf::sources::BufferView& view) {
                auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                auto& buffer = asset.buffers[bufferView.bufferIndex];

                std::visit(fastgltf::visitor { // We only care about VectorWithMime here, because we
                    // specify LoadExternalBuffers, meaning all buffers
                    // are already loaded into a vector.
                    [](auto& arg) {},
                    [&](fastgltf::sources::Vector& vector) {
                        unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset,
                            static_cast<int>(bufferView.byteLength),
                            &width, &height, &nrChannels, 4);
                        if (data) {
                            VkExtent3D imagesize;
                            imagesize.width = width;
                            imagesize.height = height;
                            imagesize.depth = 1;

                            newImage = std::make_shared<Image>(VK_FORMAT_R8G8B8A8_UNORM, imagesize, VK_IMAGE_USAGE_SAMPLED_BIT, data, true);

                            stbi_image_free(data);
                        }
                    } 
                },
                buffer.data);
            },
           
        },
        image.data);

        // if any of the attempts to load the data failed, we havent written the image
        // so handle is null
        if (newImage == nullptr) {
            return {};
        }
        else {
            return newImage;
        }
}

std::optional<std::shared_ptr<LoadedGLTF>> loadGltf(std::string_view filePath)
{
    fmt::print("Loading GLTF: {}", filePath);

    std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
    LoadedGLTF& file = *scene.get();

    fastgltf::Parser parser{};

    constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember | fastgltf::Options::AllowDouble | fastgltf::Options::LoadGLBBuffers | fastgltf::Options::LoadExternalBuffers;

    fastgltf::GltfDataBuffer data;
    data.loadFromFile(filePath);

    fastgltf::Asset gltf;

    std::filesystem::path path = filePath;

    auto type = fastgltf::determineGltfFileType(&data);
    if (type == fastgltf::GltfType::glTF) {
        auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        }
        else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
            return {};
        }
    }
    else if (type == fastgltf::GltfType::GLB) {
        auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        }
        else {
            std::cerr << "Failed to load glTF: " << fastgltf::to_underlying(load.error()) << std::endl;
            return {};
        }
    }
    else {
        std::cerr << "Failed to determine glTF container" << std::endl;
        return {};
    }

    // we can estimate the descriptors we will need accurately
    std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> sizes = { { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 } };

    file.descriptorPool.init(VulkanEngine::getDevice()->getHandle(), gltf.materials.size(), sizes);

    scene->createPipelines();

    // load samplers
    for (fastgltf::Sampler& sampler : gltf.samplers) {

        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, .pNext = nullptr };
        sampl.maxLod = VK_LOD_CLAMP_NONE;
        sampl.minLod = 0;

        sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
        sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

        file.samplers.emplace_back(sampl);
    }

    // temporal arrays for all the objects to use while creating the GLTF data
    std::vector<std::shared_ptr<Mesh>> meshes;
    std::vector<std::shared_ptr<Node>> nodes;
    std::vector<std::shared_ptr<Mesh::GLTFMaterial>> materials;

    file.images.push_back(VulkanEngine::get().m_whiteImage);

    // load all textures
    for (fastgltf::Image& image : gltf.images) {
        std::optional<std::shared_ptr<Image>> img = load_image(gltf, image);

        if (img.has_value()) {
            //file.images[image.name.c_str()] = *img;
            file.images.push_back(img.value());
        }
        else {
            // we failed to load, so lets give the slot a default error texture to not
            // completely break loading
            file.images.push_back(VulkanEngine::get().m_errorCheckerboardImage);
            std::cout << "gltf failed to load texture " << image.name << std::endl;
        }
    }

    // create buffer to hold the material data
    file.m_materialDataBuffer = std::make_unique<Buffer>(sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
    int data_index = 0;
    GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants = (GLTFMetallic_Roughness::MaterialConstants*)file.m_materialDataBuffer->getInfo().pMappedData;

    // load all materials
    for (fastgltf::Material& mat : gltf.materials) {
        std::shared_ptr<Mesh::GLTFMaterial> newMat = std::make_shared<Mesh::GLTFMaterial>();
        materials.push_back(newMat);
        file.materials[mat.name.c_str()] = newMat;

        MaterialPass passType = MaterialPass::MainColor;
        if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
            passType = MaterialPass::Transparent;
        }

        GLTFMetallic_Roughness::MaterialResources materialResources;
        // default the material textures
        materialResources.colourID = 0;
        materialResources.colorSampler = VulkanEngine::get().m_defaultSamplerLinear.get();
        materialResources.metalRoughnessID = 0;
        materialResources.metalRoughSampler = VulkanEngine::get().m_defaultSamplerLinear.get();

        // set the uniform buffer for the material data
        materialResources.dataBuffer = file.m_materialDataBuffer.get();
        // grab textures from gltf file
        if (mat.pbrData.baseColorTexture.has_value()) {
            size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
            materialResources.colourID = img+1;

            if (gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.has_value())
                materialResources.colorSampler = &file.samplers[gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value()];
        }

        GLTFMetallic_Roughness::MaterialConstants constants;
        constants.colorFactors.x = mat.pbrData.baseColorFactor[0];
        constants.colorFactors.y = mat.pbrData.baseColorFactor[1];
        constants.colorFactors.z = mat.pbrData.baseColorFactor[2];
        constants.colorFactors.w = mat.pbrData.baseColorFactor[3];

        constants.metal_rough_factors.x = mat.pbrData.metallicFactor;
        constants.metal_rough_factors.y = mat.pbrData.roughnessFactor;

        constants.colourID = materialResources.colourID;
        // write material parameters to buffer
        sceneMaterialConstants[data_index] = constants;

        // build material
        newMat->data = file.metalRoughMaterial.write_material(passType, materialResources, data_index++);
    }

    // use the same vectors for all meshes so that the memory doesnt reallocate as
    // often
    std::vector<uint32_t> indices;
    std::vector<Vertex> vertices;
    std::vector<SurfaceData> surfaceData;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        std::shared_ptr<Mesh> newmesh = std::make_shared<Mesh>(mesh.name.c_str());
        meshes.push_back(newmesh);
        file.meshes[mesh.name.c_str()] = newmesh;

        // clear the mesh arrays each mesh, we dont want to merge them by error
        //indices.clear();
        //vertices.clear();
        std::vector<Mesh::GeoSurface> surfaces;

        surfaces.reserve(mesh.primitives.size());

        for (auto&& p : mesh.primitives) {
            Mesh::GeoSurface newSurface;
            newSurface.indirectCommand.firstIndex = (uint32_t)indices.size();
            newSurface.indirectCommand.indexCount = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;
            newSurface.indirectCommand.vertexOffset = 0;
            newSurface.indirectCommand.instanceCount = 1;

            size_t initial_vtx = vertices.size();

            // load indexes
            {
                fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
                indices.reserve(indices.size() + indexaccessor.count);

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + initial_vtx);
                    });
            }

            // load vertex positions
            {
                fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                vertices.resize(vertices.size() + posAccessor.count);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                    [&](glm::vec3 v, size_t index) {
                        Vertex newvtx;
                        newvtx.position = v;
                        newvtx.normal = { 1, 0, 0 };
                        newvtx.color = glm::vec4{ 1.f };
                        newvtx.uv_x = 0;
                        newvtx.uv_y = 0;
                        vertices[initial_vtx + index] = newvtx;
                    });
            }

            // load vertex normals
            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
                    [&](glm::vec3 v, size_t index) {
                        vertices[initial_vtx + index].normal = v;
                    });
            }

            // load UVs
            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
                    [&](glm::vec2 v, size_t index) {
                        vertices[initial_vtx + index].uv_x = v.x;
                        vertices[initial_vtx + index].uv_y = v.y;
                    });
            }

            // load vertex colors
            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
                    [&](glm::vec4 v, size_t index) {
                        vertices[initial_vtx + index].color = v;
                    });
            }

            if (p.materialIndex.has_value()) {
                newSurface.material = materials[p.materialIndex.value()];
            }
            else {
                newSurface.material = materials[0];
            }

            //loop the vertices of this surface, find min/max bounds
            glm::vec3 minpos = vertices[initial_vtx].position;
            glm::vec3 maxpos = vertices[initial_vtx].position;
            for (int i = initial_vtx; i < vertices.size(); i++) {
                minpos = glm::min(minpos, vertices[i].position);
                maxpos = glm::max(maxpos, vertices[i].position);
            }
            // calculate origin and extents from the min/max, use extent lenght for radius
            newSurface.bounds.origin = (maxpos + minpos) / 2.f;
            newSurface.bounds.extents = (maxpos - minpos) / 2.f;
            newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);


            SurfaceData surfaceDatum;
            surfaceDatum.indexCount = newSurface.indirectCommand.indexCount;
            surfaceDatum.firstIndex = newSurface.indirectCommand.firstIndex;
            surfaceDatum.extents = newSurface.bounds.extents;
            surfaceDatum.origin = newSurface.bounds.origin;
            surfaceDatum.matID = newSurface.material->data.matID;
            surfaceDatum.isTransparent = newSurface.material->data.passType == MaterialPass::Transparent;
            surfaceData.push_back(surfaceDatum);

            newSurface.id = surfaceData.size() - 1;

            surfaces.push_back(newSurface);
        }

        newmesh->uploadSurfaceData(surfaces);
    }

    //std::sort(cullData.begin(), cullData.end(), [&](const auto& iA, const auto& iB) {
    //    
    //    });


    // create buffer to hold the surface and cull data
    file.m_surfaceBuffer = std::make_unique<Buffer>(sizeof(SurfaceData) * surfaceData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
    memcpy(file.m_surfaceBuffer->getInfo().pMappedData, surfaceData.data(), sizeof(SurfaceData)* surfaceData.size());

    file.createBuffers(indices, vertices);

    // load all nodes and their meshes
    for (fastgltf::Node& node : gltf.nodes) {
        std::shared_ptr<Node> newNode;

        // find if the node has a mesh, and if it does hook it to the mesh pointer and allocate it with the meshnode class
        if (node.meshIndex.has_value()) {
            newNode = std::make_shared<MeshNode>();
            static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
            scene->m_objectCount += meshes[*node.meshIndex]->getSurfaces().size();
        }
        else {
            newNode = std::make_shared<Node>();
        }

        nodes.push_back(newNode);
        file.nodes[node.name.c_str()];

        std::visit(fastgltf::visitor{ [&](fastgltf::Node::TransformMatrix matrix) {
                            memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
                        },
                       [&](fastgltf::Node::TRS transform) {
                           glm::vec3 tl(transform.translation[0], transform.translation[1],
                               transform.translation[2]);
                           glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1],
                               transform.rotation[2]);
                           glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

                           glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
                           glm::mat4 rm = glm::toMat4(rot);
                           glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);

                           newNode->localTransform = tm * rm * sm;
                       } },
            node.transform);
    }

    // run loop again to setup transform hierarchy
    for (int i = 0; i < gltf.nodes.size(); i++) {
        fastgltf::Node& node = gltf.nodes[i];
        std::shared_ptr<Node>& sceneNode = nodes[i];

        for (auto& c : node.children) {
            sceneNode->children.push_back(nodes[c]);
            nodes[c]->parent = sceneNode;
        }
    }

    // find the top nodes, with no parents
    for (auto& node : nodes) {
        if (node->parent.lock() == nullptr) {
            file.topNodes.push_back(node);
            node->refreshTransform(glm::mat4{ 1.f });
        }
    }

    std::vector<InstanceData> instData;
    std::vector<glm::mat4> transformData;
    transformData.reserve(gltf.nodes.size());

    for (auto& node : nodes) {
        if ( MeshNode* meshNode = dynamic_cast<MeshNode*>(node.get())) {
            transformData.push_back(meshNode->worldTransform);

            InstanceData instDatum;
            instDatum.transformID = transformData.size() - 1;

            for (auto& surface : meshNode->mesh->getSurfaces()) {
                instDatum.surfaceID = surface.id;

                instData.push_back(instDatum);
            }
        }
    }

    // create buffers to hold the per instance data
    file.m_transformBuffer = std::make_unique<Buffer>(sizeof(glm::mat4) * transformData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
    memcpy(file.m_transformBuffer->getInfo().pMappedData, transformData.data(), sizeof(glm::mat4)* transformData.size());

    file.m_instanceBuffer = std::make_unique<Buffer>(sizeof(InstanceData) * instData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
    memcpy(file.m_instanceBuffer->getInfo().pMappedData, instData.data(), sizeof(InstanceData)* instData.size());

    return scene; 
}

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
