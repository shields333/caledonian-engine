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
	Mesh(std::span<uint32_t> indices, std::span<Vertex> vertices);

	// push constants for our mesh object draws
	struct GPUDrawPushConstants {
		glm::mat4 worldMatrix;
		VkDeviceAddress vertexBufferAddress;
	};

	GPUDrawPushConstants& getPushConstants() { return m_pushConstants; }
	Buffer& getIndexBuffer() { return *m_indexBuffer; }

private:
	std::unique_ptr<Buffer> m_indexBuffer;
	std::unique_ptr<Buffer> m_vertexBuffer;
	VkDeviceAddress			m_vertexBufferAddress;
	
	GPUDrawPushConstants m_pushConstants{
		.worldMatrix = glm::mat4{ 1.f },
		.vertexBufferAddress = 0
	};
};