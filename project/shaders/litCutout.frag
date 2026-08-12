#version 460

#extension GL_GOOGLE_include_directive : require
#include "input_structures.glsl"
#include "draw_structures.glsl"
#include "lighting.glsl"

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColour;
layout (location = 3) in vec2 inUV;
layout (location = 4) flat in int inMatID;
layout (location = 5) in mat3 inTBN;

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

	vec4 texColour = texture(textures[material.colourID],inUV);
	if (texColour.a < material.alphaCutoff)
		discard;

	vec2 texRoughMetal = texture(textures[material.metalRoughnessID], inUV).gb;
	vec3 texEmissive = texture(textures[material.emissiveID], inUV).rgb;
	vec3 texNormal = texture(textures[material.normalID], inUV).rgb * 2.0 - 1.0;

	float roughness = texRoughMetal.r * material.metalRoughFactors.g;
	float metalness = texRoughMetal.g * material.metalRoughFactors.r;

	vec3 emissiveColour = texEmissive * material.emmisiveFactors;

	vec4 matColour = inColour * material.colourFactors * texColour;
	vec3 lightColour = sceneData.sunlightColor.rgb * sceneData.sunlightColor.w;
	vec3 ambient = matColour.xyz * sceneData.ambientColour.xyz;

	vec3 lightDir = -sceneData.sunlightDirection.xyz;
	vec3 viewDir = normalize(sceneData.cameraPos.xyz - inPos);

	vec3 normal = inTBN * texNormal;

	vec3 light = CookTorrance_BRDF(matColour.rgb, lightColour, normal, viewDir, lightDir, roughness, metalness);

	outFragColor = vec4(light + ambient + emissiveColour, matColour.w);
}