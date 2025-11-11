#version 460

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"
#include "draw_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) flat in int inMatID;

layout (location = 0) out vec4 outFragColor;

//push constants block
layout( push_constant ) uniform Constants
{
	VertexBuffer vertexBuffer;
	VisibleObjectBuffer objectBuffer;
	MaterialBuffer materialBuffer;
} pushConstants;

void main() 
{
	GLTFMaterialData material = pushConstants.materialBuffer.materials[inMatID];

	float lightValue = max(dot(inNormal, sceneData.sunlightDirection.xyz), 0.1f);

	vec4 texColour = texture(colorTex[material.colourID],inUV);

	if (texColour.a < material.alphaCutoff)
		discard;

	vec4 color = inColor * texColour;
	vec3 ambient = color.xyz *  sceneData.ambientColor.xyz;

	outFragColor = vec4(color.xyz * lightValue *  sceneData.sunlightColor.w + ambient, color.w);
}