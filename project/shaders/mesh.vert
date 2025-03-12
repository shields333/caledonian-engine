#version 460

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"
#include "draw_structures.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out uint outColourID;

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

	gl_Position =  sceneData.viewproj * objectData.modelMatrix * position;

	GLTFMaterialData material = pushConstants.materialBuffer.materials[objectData.matID];

	outNormal = (objectData.modelMatrix * vec4(v.normal, 0.f)).xyz;
	outColor = v.color.xyz * material.colorFactors.xyz;	
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;

	outColourID = material.colourID;
}