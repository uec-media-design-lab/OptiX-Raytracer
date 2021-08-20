#pragma once 

#include <sutil/vec_math.h>
#include <oprt/optix/macros.h>

namespace oprt {

struct Ray {

    float3 at(const float time) { return o + d*time; }

    /* Position of ray origin in world coordinates. */
    float3 o;

    /* Direction of out-going ray from origin. */
    float3 d;

    /* Time of ray. It is mainly used for realizing motion blur. */
    float tmin;
    float tmax;
    float t;

    /* Spectrum information of ray. */
    float3 spectrum;
};

struct pRay {
    /** @todo Polarized ray */
    float3 at(const float time) { return o + d*time; }

    float3 o;
    float3 d; 
    float3 tangent; // tangent vector

    float tmin; 
    float tmax; 
    float t;

    float3 spectrum;
};

/** Useful function on OptiX to get ray info */
#ifdef __CUDACC__
INLINE DEVICE Ray getLocalRay() {
    Ray ray;
    ray.o = optixTransformPointFromWorldToObjectSpace( optixGetWorldRayOrigin() );
    ray.d = normalize( optixTransformVectorFromWorldToObjectSpace( optixGetWorldRayDirection() ) );
    ray.tmin = optixGetRayTmin();
    ray.tmax = optixGetRayTmax();
    ray.t = optixGetRayTime();
    return ray;
}

INLINE DEVICE Ray getWorldRay() {
    Ray ray;
    ray.o = optixGetWorldRayOrigin();
    ray.d = normalize( optixGetWorldRayDirection() );
    ray.tmin = optixGetRayTmin();
    ray.tmax = optixGetRayTmax();
    ray.t = optixGetRayTime();
    return ray;
}

INLINE DEVICE pRay getLocalpRay() 
{
    
}

INLINE DEVICE pRay getWorldpRay() 
{

}

#endif

} // ::oprt