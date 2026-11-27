#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

layout(set = 0, binding = 0) uniform  SceneData{   

	mat4 view;
	mat4 proj;
	mat4 viewproj;
	vec4 ambientColour;
	vec4 sunlightDirection; //w for sun power
	vec4 sunlightColor;
	vec4 cameraPos;
	vec2 hizSize;
} sceneData;

struct Vertex {

	vec3 position;
	float uv_x;
	vec3 normal;
	float uv_y;
	vec4 color;
	vec4 tangent;
}; 

layout(buffer_reference, std430) readonly buffer VertexBuffer{ 
	Vertex vertices[];
};

struct GLTFMaterialData{   
	vec4 colourFactors;
	vec3 emmisiveFactors;
	float normalScale;
	vec2 metalRoughFactors;
	uint colourID;
	uint metalRoughnessID;
	uint normalID;
	uint emissiveID;
	float alphaCutoff;
};

layout(buffer_reference, std430) readonly buffer MaterialBuffer{ 
	GLTFMaterialData materials[];
};

