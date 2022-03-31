#pragma once 

#include <prayground/math/matrix.h>
#include <prayground/math/vec.h>

using namespace prayground;

struct AreaEmitterInfo
{
    void* shape_data;
    Matrix4f objToWorld;
    Matrix4f worldToObj;

    uint32_t sample_id;
    uint32_t pdf_id;
};

struct LightInteraction
{
    // A surface point on the light source in world coordinates
    Vec3f p;
    // Surface normal on the light source in world coordinates
    Vec3f n;
    // Texture coordinates on light source
    Vec2f uv;
    // Area of light source
    float area;
    // PDF of light source
    float pdf;
};

struct PathVertex {
    // Hit position
    Vec3f       p;
    // Path throughput
    Vec3f       throughput;
    // Path length between source and vertex
    uint32_t    path_length;

    // Surface infomation on a vertex
    SurfaceInfo surface_info;
    bool from_light;

    // MIS quantities
    float dVCM;
    float dVM; 
    float dVC;
};

struct LaunchParams {
    uint32_t width; 
    uint32_t height;
    uint32_t samples_per_launch;
    int frame;

    Vec4f* accum_buffer;
    Vec4u* result_buffer;

    AreaEmitterInfo* lights;
    
    // Stored light vertices 
    PathVertex* light_vertices;
};