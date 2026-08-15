#pragma once

#include "vk_types.h"

#include "Buffer.h"

struct MaterialInstance;

struct alignas (16) Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
	glm::vec4 tangent;
};

class Mesh
{
public:
	Mesh(std::string name);
	Mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

	struct Bounds {
		glm::vec3 origin;
		float sphereRadius;
		glm::vec3 extents;
	};

	struct GeoSurface {
		VkDrawIndexedIndirectCommand indirectCommand;
		Bounds bounds;
		std::shared_ptr<MaterialInstance> material;
		uint32_t id;
	};

	// push constants for our mesh object draws
	struct GPUDrawPushConstants {
		VkDeviceAddress vertexBufferAddress;
		VkDeviceAddress objectBufferAddress;
		VkDeviceAddress materialBufferAddress;
	};

	void createBuffers(std::span<uint32_t> indices, std::span<Vertex> vertices);
	void uploadSurfaceData(std::vector<GeoSurface>& surfaces);

	void setBuffers(std::shared_ptr<Buffer> vertexBuf, std::shared_ptr<Buffer> indexBuf);
	GPUDrawPushConstants& getPushConstants() { return m_pushConstants; }
	const Buffer& getIndexBuffer() const { return *m_indexBuffer; }
	const Buffer& getVertexBuffer() const { return *m_vertexBuffer; }
	std::vector<GeoSurface>& getSurfaces() { return m_surfaces; }
	std::string getName() { return m_name; }

private:
	std::string m_name;

	std::shared_ptr<Buffer> m_indexBuffer;
	std::shared_ptr<Buffer> m_vertexBuffer;
	
	std::vector<GeoSurface> m_surfaces;

	GPUDrawPushConstants m_pushConstants{
		.vertexBufferAddress = 0,
		.objectBufferAddress = 0,
		.materialBufferAddress = 0
	};
};