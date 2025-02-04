#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0) uniform  SceneData{   

	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColor;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
} sceneData;

layout(set = 1, binding = 0) uniform sampler2D colorTex[];

struct Vertex {

	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
}; 

layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
	Vertex vertices[];
};

struct ObjectData{
	mat4 modelMatrix;
	uint materialID;
};

layout(buffer_reference, std430) readonly buffer ObjectBuffer{
	ObjectData data[];
};

struct GLTFMaterialData{   
	vec4 colorFactors;
	vec2 metal_rough_factors;
	uint colourID;
};

layout(buffer_reference, std430) readonly buffer MaterialBuffer{ 
	GLTFMaterialData materials[];
};

//push constants block
layout( push_constant ) uniform Constants
{
	VertexBuffer vertexBuffer;
	ObjectBuffer objectBuffer;
	MaterialBuffer materialBuffer;
} pushConstants;
