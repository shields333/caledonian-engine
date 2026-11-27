#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_shader_image_load_formatted : require
//#extension GL_KHR_shader_subgroup_clustered : require

#define GROUP_SIZE 1024
#define THREADS_X 32
#define THREADS_Y 32

layout (set = 0, binding = 0) uniform sampler2D inputTex;
layout (set = 0, binding = 1) uniform image2D outputImg[12];
layout (set = 0, binding = 2) uniform coherent image2D midImage;

layout(buffer_reference, std430) buffer AtomicCounter{
    uint counter;
};

layout( push_constant ) uniform Constants
{
    AtomicCounter atomicCounter;
    uint baseMip;
    uint mipCount;
    vec2 hizExtents;
} pushConstants;


// Shared
shared vec4 sharedData[THREADS_X][THREADS_Y];

layout (local_size_x = THREADS_X, local_size_y = THREADS_Y) in;

void calc4(uvec2 threadDims, inout uint outputMip, uvec2 binOffset, uvec2 writeBin, uvec2 groupID)
{
    for (int i = 0; i < 4; i++){
        barrier();
        vec4 mip;
        if (++outputMip < pushConstants.mipCount && all(lessThan(gl_LocalInvocationID.xy, threadDims))){
            mip = compFunc(sharedData[gl_LocalInvocationID.x][gl_LocalInvocationID.y],
                            sharedData[gl_LocalInvocationID.x + binOffset.x][gl_LocalInvocationID.y],
                            sharedData[gl_LocalInvocationID.x][gl_LocalInvocationID.y + binOffset.y],              
                            sharedData[gl_LocalInvocationID.x + binOffset.x][gl_LocalInvocationID.y + binOffset.y])

            imageStore(outputImg[outputMip], ivec2(gl_LocalInvocationID.xy + threadDims * groupID), mip);
        }
        barrier();
        if (outputMip < pushConstants.mipCount && all(lessThan(gl_LocalInvocationID.xy, threadDims))){
            threadDims >>= 1;
            uvec2 sharedID = writeBin + gl_LocalInvocationID.xy / 2;
            sharedData[sharedID.x][sharedID.y] = mip;
        }
    }
}

void downSample()
{
    uvec2 threadDims = uvec2(THREADS_X, THREADS_Y) / 2;
    uint outputMip = pushConstants.baseMip;
    uvec2 binOffset = uvec2(THREADS_X, THREADS_Y) / 2;
    uvec2 writeBin = uvec2(gl_LocalInvocationID.xy % 2) * binOffset;

    // write out mip 1
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / vec2(pushConstants.hizExtents);
    vec4 mip = textureLod(inputTex, uv, 0.0);
    imageStore(outputImg[outputMip], ivec2(gl_GlobalInvocationID.xy), mip);

    uvec2 sharedID = writeBin + gl_LocalInvocationID.xy / 2;
    sharedData[sharedID.x][sharedID.y] = mip;

    calc4(threadDims, outputMip, binOffset, writeBin, gl_WorkGroupID.xy);

    // the last mip of the workgroup needs to be written to the coherent image
    barrier();
    if (++outputMip < pushConstants.mipCount && all(equal(gl_LocalInvocationID.xy, uvec2(0)))){
        mip = compFunc(sharedData[gl_LocalInvocationID.x][gl_LocalInvocationID.y],
                        sharedData[gl_LocalInvocationID.x + binOffset.x][gl_LocalInvocationID.y],
                        sharedData[gl_LocalInvocationID.x][gl_LocalInvocationID.y + binOffset.y],              
                        sharedData[gl_LocalInvocationID.x + binOffset.x][gl_LocalInvocationID.y + binOffset.y])

        imageStore(midImage, ivec2(gl_LocalInvocationID.xy + vec2(1) * gl_WorkGroupID.xy), mip);
    }

    memoryBarrierImage();
    barrier();

    uint numFinished;
    if (gl_LocalInvocationIndex == 0){
        numFinished = atomicAdd(pushConstants.atomicCounter.counter, 1u)+1;
        sharedData[0][0].x = numFinished;
    }
    uint totalWorkGroups = gl_NumWorkGroups.x * gl_NumWorkGroups.y * gl_NumWorkGroups.z;

    barrier();

    // the last workgroup to finish will calculate the last 6 mips
    if (++outputMip < pushConstants.mipCount && sharedData[0][0].x >= totalWorkGroups){
        threadDims = max(uvec2(pow(2, pushConstants.mipCount - outputMip - 1)), uvec2(THREADS_X, THREADS_Y)) / 2;
            
        // write out mip 7
        ivec2 id = ivec2(gl_LocalInvocationID.xy * 2u);
        vec4 mip = compFunc(imageLoad(midImage, id),
                            imageLoad(midImage, ivec2(id.x+1, id.y)),
                            imageLoad(midImage, ivec2(id.x, id.y+1)),
                            imageLoad(midImage, ivec2(id.x+1, id.y+1)))

        imageStore(outputImg[outputMip], ivec2(gl_LocalInvocationID.xy), mip);

        uvec2 sharedID = writeBin + gl_LocalInvocationID.xy / 2;
        sharedData[sharedID.x][sharedID.y] = mip;

        calc4(threadDims, outputMip, binOffset, writeBin, uvec2(0));

        barrier();
        if (++outputMip < pushConstants.mipCount && all(equal(gl_LocalInvocationID.xy, uvec2(0)))){
            mip = compFunc(sharedData[gl_LocalInvocationID.x][gl_LocalInvocationID.y],
                            sharedData[gl_LocalInvocationID.x + binOffset.x][gl_LocalInvocationID.y],
                            sharedData[gl_LocalInvocationID.x][gl_LocalInvocationID.y + binOffset.y],              
                            sharedData[gl_LocalInvocationID.x + binOffset.x][gl_LocalInvocationID.y + binOffset.y])

            imageStore(outputImg[outputMip], ivec2(gl_LocalInvocationID.xy + vec2(1) * gl_WorkGroupID.xy), mip);
        }
    }

    // write out mip 5
    // if (++outputMip < pushConstants.mipCount && gl_LocalInvocationIndex == 0){
    //     mip = sharedData[0][0];
    //     mip += sharedData[1][0];
    //     mip += sharedData[0][1];
    //     mip += sharedData[1][1];
    //     mip /= 4;

    //     imageStore(outputImg[outputMip], ivec2(gl_WorkGroupID.xy), mip);
    // }

    // outputExtents /= 2;
    // mip = subgroupClusteredAdd(mip, 4);
    // if ((gl_SubgroupInvocationID % 4) == 0)
    //     imageStore(outputImg[++outputMip], outputExtents, mip / 4.0);

    // outputExtents /= 2;
    // mip = subgroupClusteredAdd(mip, 16);
    // if ((gl_SubgroupInvocationID % 16) == 0){
    //     imageStore(outputImg[++outputMip], outputExtents, mip / 16.0);

    //     gl_LocalInvocationID
    //     uint offset = gl_SubgroupInvocationID / 16 * gl_NumSubgroups;
    //     sharedData[gl_SubgroupID + offset] = mip;
    // }
};