#include "GLTFLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>

#include <glm/gtx/quaternion.hpp>

#include <iostream>
#include <thread>
#include <atomic>

#include "vk_engine.h"
#include "Image.h"
#include "vk_descriptors.h"


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

    file.images.resize(gltf.images.size() + 1);
    file.images.front() = VulkanEngine::get().m_whiteImage;

    uint8_t maxThreads = std::thread::hardware_concurrency() - 1;
    if (maxThreads > gltf.images.size())
        maxThreads = gltf.images.size();

    std::vector<std::thread> textureThreads(maxThreads);
    for (auto& t : textureThreads) {
        std::atomic<int> pendingImages(gltf.images.size() - 1);
        t = std::thread([&]()
            {
                int idx = pendingImages.fetch_sub(1);
                // load all textures
                while (idx >= 0) {
                    std::optional<std::shared_ptr<Image>> img = load_image(gltf, gltf.images[idx]);

                    if (img.has_value()) {
                        file.images[idx + 1] = img.value();
                    }
                    else {
                        // we failed to load, so lets give the slot a default error texture to not
                        // completely break loading
                        file.images[idx + 1] = VulkanEngine::get().m_errorCheckerboardImage;
                        std::cout << "gltf failed to load texture " << gltf.images[idx].name << std::endl;
                    }
                    idx = pendingImages.fetch_sub(1);
                }
            });
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
            materialResources.colourID = img + 1;

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

    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;

    for (fastgltf::Mesh& mesh : gltf.meshes) {
        for (auto&& p : mesh.primitives) {
            fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
            indexCount += indexaccessor.count;

            fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
            vertexCount += posAccessor.count;
        }
    }
    indices.reserve(indexCount);
    vertices.reserve(vertexCount);

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

            size_t buffOffset = vertices.size();

            // load indexes
            {
                fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];

                fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
                    [&](std::uint32_t idx) {
                        indices.push_back(idx + buffOffset);
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
                        vertices[buffOffset + index] = newvtx;
                    });
            }

            // load vertex normals
            auto normals = p.findAttribute("NORMAL");
            if (normals != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
                    [&](glm::vec3 v, size_t index) {
                        vertices[buffOffset + index].normal = v;
                    });
            }

            // load UVs
            auto uv = p.findAttribute("TEXCOORD_0");
            if (uv != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
                    [&](glm::vec2 v, size_t index) {
                        vertices[buffOffset + index].uv_x = v.x;
                        vertices[buffOffset + index].uv_y = v.y;
                    });
            }

            // load vertex colors
            auto colors = p.findAttribute("COLOR_0");
            if (colors != p.attributes.end()) {

                fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
                    [&](glm::vec4 v, size_t index) {
                        vertices[buffOffset + index].color = v;
                    });
            }

            if (p.materialIndex.has_value()) {
                newSurface.material = materials[p.materialIndex.value()];
            }
            else {
                newSurface.material = materials[0];
            }

            //loop the vertices of this surface, find min/max bounds
            glm::vec3 minpos = vertices[buffOffset].position;
            glm::vec3 maxpos = vertices[buffOffset].position;
            for (int i = buffOffset; i < vertices.size(); i++) {
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

    // create buffer to hold the surface and cull data
    file.m_surfaceBuffer = std::make_unique<Buffer>(sizeof(SurfaceData) * surfaceData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
    memcpy(file.m_surfaceBuffer->getInfo().pMappedData, surfaceData.data(), sizeof(SurfaceData) * surfaceData.size());

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
        if (MeshNode* meshNode = dynamic_cast<MeshNode*>(node.get())) {
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
    memcpy(file.m_transformBuffer->getInfo().pMappedData, transformData.data(), sizeof(glm::mat4) * transformData.size());

    file.m_instanceBuffer = std::make_unique<Buffer>(sizeof(InstanceData) * instData.size(),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO);
    memcpy(file.m_instanceBuffer->getInfo().pMappedData, instData.data(), sizeof(InstanceData) * instData.size());

    for (auto& t : textureThreads)
        t.join();

    file.createBuffers(indices, vertices);

    return scene;
}
