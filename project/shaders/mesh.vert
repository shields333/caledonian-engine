#version 460

#extension GL_GOOGLE_include_directive : require

#include "input_structures.glsl"

layout (location = 0) out vec3 outNormal;
layout (location = 1) out vec3 outColor;
layout (location = 2) out vec2 outUV;
layout (location = 3) out uint outColourID;

void main() 
{
	Vertex v = pushConstants.vertexBuffer.vertices[gl_VertexIndex];
	
	vec4 position = vec4(v.position, 1.0f);

	gl_Position =  sceneData.viewproj * pushConstants.render_matrix *position;

	GLTFMaterialData material = pushConstants.materialBuffer.materials[gl_BaseInstance];

	outNormal = (pushConstants.render_matrix * vec4(v.normal, 0.f)).xyz;
	outColor = v.color.xyz * material.colorFactors.xyz;	
	outUV.x = v.uv_x;
	outUV.y = v.uv_y;

	outColourID = material.colourID;
}