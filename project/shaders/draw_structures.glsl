#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_buffer_reference : require

struct ObjectData{
	mat4 modelMatrix;
    uint matID;
};

layout(buffer_reference, std430) buffer VisibleObjectBuffer{
	ObjectData data[];
};

