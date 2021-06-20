#pragma once

#include <sutil/vec_math.h>
#include "../../core/material.h"
#include "../../core/ray.h"
#include "../../optix/sbt.h"

namespace oprt {

struct CylinderData
{
    float radius; 
    float height;
};

#ifdef __CUDACC__

CALLABLE_FUNC void IS_FUNC(cylinder)()
{
    const HitGroupData* data = reinterpret_cast<HitGroupData*>(optixGetSbtDataPointer());
    const CylinderData* cylinder = reinterpret_cast<CylinderData*>(data->shapedata);

    const float radius = cylinder->radius;
    const float height = cylinder->height;

    Ray ray = getLocalRay();
    
    const float a = dot(ray.d, ray.d) - ray.d.y * ray.d.y;
    const float half_b = (ray.o.x * ray.d.x + ray.o.z * ray.d.z);
    const float c = dot(ray.o, ray.o) - ray.o.y * ray.o.y - radius*radius;
    const float discriminant = half_b*half_b - a*c;

    if (discriminant > 0.0f)
    {
        bool hit_side = true;
        const float sqrtd = sqrtf(discriminant);
        const float side_t1 = (-half_b - sqrtd) / a;
        const float side_t2 = (-half_b + sqrtd) / a;

        const float side_tmin = fmin( side_t1, side_t2 );
        const float side_tmax = fmax( side_t1, side_t2 );

        if ( (side_tmin < ray.tmin || side_tmin > ray.tmax) && 
             (side_tmax < ray.tmin || side_tmax > ray.tmax) )
            return;

        const float upper = height / 2.0f;
        const float lower = -height / 2.0f;
        const float y_tmin = fmin( (lower - ray.o.y) / ray.d.y, (upper - ray.o.y) / ray.d.y );
        const float y_tmax = fmax( (lower - ray.o.y) / ray.d.y, (upper - ray.o.y) / ray.d.y );

        float t = fmax(y_tmin, side_tmin);
        if (t > ray.tmax)
            return;

        if (t >= ray.tmin)
        {
            float3 P = ray.at(t);
            float3 normal = y_tmin > side_tmin 
                          ? normalize(P - make_float3(P.x, 0.0f, P.z))   // Hit at disk
                          : normalize(P - make_float3(0.0f, P.y, 0.0f)); // Hit at side
            optixReportIntersection(t, 0, float3_as_ints(normal));
        }
        else 
        {
            t = fmin(y_tmax, side_tmax);
            if (t < ray.tmin)
                return;

            float3 P = ray.at(t);
            float3 normal = y_tmax < side_tmax 
                          ? normalize(P - make_float3(P.x, 0.0f, P.z))   // Hit at disk
                          : normalize(P - make_float3(0.0f, P.y, 0.0f)); // Hit at side
            optixReportIntersection(t, 0, float3_as_ints(normal));
        }
    }
}

CALLABLE_FUNC void CH_FUNC(cylinder)()
{
    const HitGroupData* data = reinterpret_cast<HitGroupData*>(optixGetSbtDataPointer());
    const CylinderData* cylinder = reinterpret_cast<CylinderData*>(data->shapedata);

    oprt::Ray ray = getWorldRay();

    float3 local_n = make_float3(
        int_as_float( optixGetAttribute_0() ),
        int_as_float( optixGetAttribute_1() ), 
        int_as_float( optixGetAttribute_2() )
    );
    float3 world_n = optixTransformNormalFromObjectToWorldSpace(local_n);
    world_n = normalize(world_n);

    SurfaceInteraction* si = getSurfaceInteraction();
    si->p = ray.at(ray.tmax);
    si->n = world_n;
    si->wi = ray.d;
    si->uv = make_float2(1.0f);

    si->mat_property = {
        data->matdata,               // material data
        data->material_type * 2,     // id of callable function to evaluate bsdf and for importance sampling
        data->material_type * 2 + 1  // id of callable function to evaluate pdf
    };
}

CALLABLE_FUNC void CH_FUNC(cylinder_occlusion)()
{
    setPayloadOcclusion(true);
}

#endif

}