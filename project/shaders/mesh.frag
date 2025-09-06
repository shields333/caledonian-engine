#version 460

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"

layout (location = 0) in vec3 inNormal;
layout (location = 1) in vec4 inColor;
layout (location = 2) in vec2 inUV;
layout (location = 3) flat in int inColourID;

layout (location = 0) out vec4 outFragColor;

void main() 
{
	float lightValue = max(dot(inNormal, sceneData.sunlightDirection.xyz), 0.1f);

	vec4 texColour = texture(colorTex[inColourID],inUV);
	vec4 color = inColor * texColour;
	vec3 ambient = color.xyz *  sceneData.ambientColor.xyz;

	outFragColor = vec4(color.xyz * lightValue *  sceneData.sunlightColor.w + ambient, color.w);
}