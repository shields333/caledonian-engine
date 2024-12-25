#include "Mesh.h"

#include "vk_engine.h"

Mesh::Mesh(std::string name) :
	m_name(name)
{
}

Mesh::Mesh(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	createBuffers(indices, vertices);
}

void Mesh::createBuffers(std::span<uint32_t> indices, std::span<Vertex> vertices)
{
	// shouldn't overwrite an existing mesh
	assert(!m_indexBuffer);

	const size_t vertexBufferSize = vertices.size() * sizeof(Vertex);
	const size_t indexBufferSize = indices.size() * sizeof(uint32_t);

	//create vertex buffer
	m_vertexBuffer = std::make_unique<Buffer>(vertexBufferSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
		VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

	//find the adress of the vertex buffer
	VkBufferDeviceAddressInfo deviceAdressInfo{ .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,.buffer = m_vertexBuffer->getHandle() };
	m_vertexBufferAddress = m_pushConstants.vertexBufferAddress = vkGetBufferDeviceAddress(VulkanEngine::getDevice()->getHandle(), &deviceAdressInfo);

	//create index buffer
	m_indexBuffer = std::make_unique<Buffer>(indexBufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
}

void Mesh::uploadSurfaceData(std::vector<GeoSurface>& surfaces)
{
	m_surfaces.insert(m_surfaces.begin(), surfaces.begin(), surfaces.end());
}
