#pragma once

#include "vk_types.h"

#include "Buffer.h"

struct Vertex {
	glm::vec3 position;
	float uv_x;
	glm::vec3 normal;
	float uv_y;
	glm::vec4 color;
};

class Mesh
{
public:
	Mesh(std::string name);
	Mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

	struct GLTFMaterial {
		MaterialInstance data;
	};

	struct Bounds {
		glm::vec3 origin;
		float sphereRadius;
		glm::vec3 extents;
	};

	struct GeoSurface {
		VkDrawIndexedIndirectCommand indirectCommand;
		Bounds bounds;
		std::shared_ptr<GLTFMaterial> material;
	};

	// push constants for our mesh object draws
	struct GPUDrawPushConstants {
		glm::mat4 worldMatrix;
		VkDeviceAddress vertexBufferAddress;
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
		.worldMatrix = glm::mat4{ 1.f },
		.vertexBufferAddress = 0
	};
};