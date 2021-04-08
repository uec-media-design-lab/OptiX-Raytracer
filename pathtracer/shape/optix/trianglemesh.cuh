#pragma once

#include <sutil/vec_math.h>
#include <include/core/util.h>
#include <include/core/transform.h>
#include <include/core/material.h>
#include <include/optix/sbt.h>

namespace pt {

struct MeshData {
    float3* vertices;
    float3* normals;
    int3* indices;
    // Transform transform;
};

}

#ifdef __CUDACC__

CALLABLE_FUNC void CH_FUNC(mesh)()
{
    const pt::HitGroupData* data = reinterpret_cast<pt::HitGroupData*>(optixGetSbtDataPointer());
    const pt::MeshData* mesh_data = reinterpret_cast<pt::MeshData*>(data->shapedata);
    
    const int prim_idx = optixGetPrimitiveIndex();
    const int3 index = mesh_data->indices[prim_idx];
    const float3 ro = optixGetWorldRayOrigin();
    const float3 rd = optixGetWorldRayDirection();
    const float u = optixGetTriangleBarycentrics().x;
    const float v = optixGetTriangleBarycentrics().y;

    const float tmin = optixGetRayTmin();
    const float tmax = optixGetRayTmax();

	const float3 n0 = normalize(mesh_data->normals[index.x]);
	const float3 n1 = normalize(mesh_data->normals[index.y]);
	const float3 n2 = normalize(mesh_data->normals[index.z]);

    // Linear interpolation of normal by barycentric coordinates.
    float3 n = normalize( (1.0f-u-v)*n0 + u*n1 + v*n2 );
    n = faceforward(n, -rd, n);

    pt::SurfaceInteraction* si = get_surfaceinteraction();
    si->p = ro + tmax*rd;
    si->n = n;
    si->wi = rd;

    /**
     * \brief Direct callable function
     * template <typename ReturnT, typename... Args>
     * ReturnT optixDirectCall( unsigned int sbtIndex, ArgTypes... args );
     */
    
    // optixDirectCall<void, pt::SurfaceInteraction*, const pt::HitGroupData*>(data->sample_func_idx, si, data);
    
    si->radiance = make_float3(fabs(n.x), fabs(n.y), fabs(n.z));
}

#endif