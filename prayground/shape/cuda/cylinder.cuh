#pragma once

#include <prayground/math/vec_math.h>
#include <prayground/core/material.h>
#include <prayground/core/ray.h>
#include <prayground/optix/sbt.h>
#include <prayground/optix/cuda/util.cuh>

namespace prayground {

#ifdef __CUDACC__

static INLINE DEVICE float2 getUV(
    const float3& p, const float radius, const float height, const bool hit_disk
)
{
    if (hit_disk)
    {
        const float r = sqrtf(p.x*p.x + p.z*p.z) / radius;
        const float theta = atan2(p.z, p.x);
        float u = 1.0f - (theta + math::pi/2.0f) / math::pi;
        return make_float2(u, r);
    } 
    else
    {
        const float theta = atan2(p.z, p.x);
        const float v = (p.y + height / 2.0f) / height;
        float u = 1.0f - (theta + math::pi/2.0f) / math::pi;
        return make_float2(u, v);
    }
}

CALLABLE_FUNC void IS_FUNC(cylinder)()
{
    const HitGroupData* data = reinterpret_cast<HitGroupData*>(optixGetSbtDataPointer());
    const CylinderData* cylinder = reinterpret_cast<CylinderData*>(data->shape_data);

    const float radius = cylinder->radius;
    const float height = cylinder->height;

    Ray ray = getLocalRay();
    
    const float a = dot(ray.d, ray.d) - ray.d.y * ray.d.y;
    const float half_b = (ray.o.x * ray.d.x + ray.o.z * ray.d.z);
    const float c = dot(ray.o, ray.o) - ray.o.y * ray.o.y - radius*radius;
    const float discriminant = half_b*half_b - a*c;

    if (discriminant > 0.0f)
    {
        const float sqrtd = sqrtf(discriminant);
        const float side_t1 = (-half_b - sqrtd) / a;
        const float side_t2 = (-half_b + sqrtd) / a;

        const float side_tmin = fmin( side_t1, side_t2 );
        const float side_tmax = fmax( side_t1, side_t2 );

        if ( side_tmin > ray.tmax || side_tmax < ray.tmin )
            return;

        const float upper = height / 2.0f;
        const float lower = -height / 2.0f;
        const float y_tmin = fmin( (lower - ray.o.y) / ray.d.y, (upper - ray.o.y) / ray.d.y );
        const float y_tmax = fmax( (lower - ray.o.y) / ray.d.y, (upper - ray.o.y) / ray.d.y );

        float t1 = fmax(y_tmin, side_tmin);
        float t2 = fmin(y_tmax, side_tmax);
        if (t1 > t2 || (t2 < ray.tmin) || (t1 > ray.tmax))
            return;
        
        bool check_second = true;
        if (ray.tmin < t1 && t1 < ray.tmax)
        {
            float3 P = ray.at(t1);
            bool hit_disk = y_tmin > side_tmin;
            float3 normal = hit_disk 
                          ? normalize(P - make_float3(P.x, 0.0f, P.z))   // Hit at disk
                          : normalize(P - make_float3(0.0f, P.y, 0.0f)); // Hit at side
            float2 uv = getUV(P, radius, height, hit_disk);
            optixReportIntersection(t1, 0, float3_as_ints(normal), float2_as_ints(uv));
            check_second = false;
        }
        
        if (check_second)
        {
            if (ray.tmin < t2 && t2 < ray.tmax)
            {
                float3 P = ray.at(t2);
                bool hit_disk = y_tmax < side_tmax;
                float3 normal = hit_disk
                            ? normalize(P - make_float3(P.x, 0.0f, P.z))   // Hit at disk
                            : normalize(P - make_float3(0.0f, P.y, 0.0f)); // Hit at side
                float2 uv = getUV(P, radius, height, hit_disk);
                optixReportIntersection(t2, 0, float3_as_ints(normal), float2_as_ints(uv));
            }
        }
    }
}

CALLABLE_FUNC void CH_FUNC(cylinder)()
{
    const HitGroupData* data = reinterpret_cast<HitGroupData*>(optixGetSbtDataPointer());
    const CylinderData* cylinder = reinterpret_cast<CylinderData*>(data->shape_data);

    prayground::Ray ray = getWorldRay();

    float3 local_n = make_float3(
        int_as_float( optixGetAttribute_0() ),
        int_as_float( optixGetAttribute_1() ), 
        int_as_float( optixGetAttribute_2() )
    );

    float2 uv = make_float2(
        int_as_float( optixGetAttribute_3() ),
        int_as_float( optixGetAttribute_4() )
    );

    float3 world_n = optixTransformNormalFromObjectToWorldSpace(local_n);

    SurfaceInteraction* si = getSurfaceInteraction();
    si->p = ray.at(ray.tmax);
    si->n = normalize(world_n);
    si->wi = ray.d;
    si->uv = uv;

    si->surface_type = data->surface_type;
    si->surface_property = {
        data->surface_data,
        data->surface_func_base_id
    };
}

CALLABLE_FUNC void CH_FUNC(cylinder_occlusion)()
{
    setPayloadOcclusion(true);
}

#endif

}