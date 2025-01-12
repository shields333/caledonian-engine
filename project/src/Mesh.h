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

	struct GeoSurface {
		uint32_t startIndex;
		uint32_t count;
		std::shared_ptr<GLTFMaterial> material;
	};

	// push constants for our mesh object draws
	struct GPUDrawPushConstants {
		glm::mat4 worldMatrix;
		VkDeviceAddress vertexBufferAddress;
	};

	void createBuffers(std::span<uint32_t> indices, std::span<Vertex> vertices);
	void uploadSurfaceData(std::vector<GeoSurface>& surfaces);

	GPUDrawPushConstants& getPushConstants() { return m_pushConstants; }
	Buffer& getIndexBuffer() { return *m_indexBuffer; }
	std::vector<GeoSurface>& getSurfaces() { return m_surfaces; }
	VkDeviceAddress getBufferAddress() { return m_vertexBufferAddress; }
	std::string getName() { return m_name; }

private:
	std::string m_name;

	std::unique_ptr<Buffer> m_indexBuffer;
	std::unique_ptr<Buffer> m_vertexBuffer;
	VkDeviceAddress			m_vertexBufferAddress;
	
	std::vector<GeoSurface> m_surfaces;

	GPUDrawPushConstants m_pushConstants{
		.worldMatrix = glm::mat4{ 1.f },
		.vertexBufferAddress = 0
	};
};