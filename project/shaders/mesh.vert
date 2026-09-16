#version 460

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"
#include "draw_structures.glsl"

layout (location = 0) out vec3 outPos;
layout (location = 1) out vec4 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out uint outMatID;
layout (location = 4) out mat3 outTBN;

//push constants block
layout( push_constant ) uniform Constants
{
	VertexBuffer vertexBuffer;
	VisibleObjectBuffer objectBuffer;
	MaterialBuffer materialBuffer;
} pushConstants;

void main() 
{	
	Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
	ObjectData objectData = pushConstants.objectBuffer.data[gl_BaseInstance];

	vec4 position = vec4(v.position, 1.0f);
	position = objectData.modelMatrix * position;
	outPos = position.xyz;

	gl_Position =  sceneData.viewproj * position;
	
	vec3 normal = mat3(objectData.modelMatrix) * v.normal;
	vec3 tangent = mat3(objectData.modelMatrix) * v.tangent.xyz;
	vec3 bitangent = cross(normal, tangent) * v.tangent.w;
	outTBN = mat3(tangent, bitangent, normal);
	
	outColor = v.color;	
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;

	outMatID = objectData.matID;
}