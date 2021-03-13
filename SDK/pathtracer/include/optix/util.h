#include <optix.h>

#include <sutil/vec_math.h>
#include <cuda/helpers.h>

#include <core/util.h>

namespace pt {

/** MEMO: 
 * If we need to take into account spectral property (not RGB), we should
 * switch Spectrum representation.
 * 
 * If Spectrum is rgb, rgb is float3? char3? I'm not sure which one is better.
 * 
 * NOTE: Currently, `Spectrum` must be a float3 */

enum RayType {
    RAY_TYPE_RADIANCE = 0,
    RAY_TYPE_OCCLUSION = 1,
    RAY_TYPE_COUNT = 2
};

struct SurfaceInteraction {
    /** position of intersection point in world coordinates. */
    float3 p;

    /** Surface normal of primitive at intersection point. */
    float3 n;

    /** Incident and outgoing directions at a surface. */
    float3 wi;
    float3 wo;

    /** Spectrum information of ray. */
    float3 spectrum;

    /** radiance and attenuation term computed by a material attached with surface. */
    float3 radiance;
    float3 attenuation;
    float3 emission;

    /** UV coordinate at intersection point. */
    float2 uv;

    /** Derivatives on texture coordinates. */
    float dpdu, dpdv;

    /** seed for random */
    unsigned int seed;

    int trace_terminate;
};

INLINE DEVICE void* unpack_pointer( unsigned int i0, unsigned int i1 )
{
    const unsigned long long uptr = static_cast<unsigned long long>( i0 ) << 32 | i1;
    void* ptr = reinterpret_cast<void*>( uptr );
    return ptr;
}

INLINE DEVICE void pack_pointer(void* ptr, unsigned int& i0, unsigned int i1)
{
    const unsigned long long uptr = reinterpret_cast<unsigned long long>( ptr );
    i0 = uptr >> 32;
    i1 = uptr & 0x00000000ffffffff;
}

INLINE DEVICE SurfaceInteraction* get_surfaceinteraction()
{
    const unsigned int u0 = optixGetPayload_0();
    const unsigned int u1 = optixGetPayload_1();
    return reinterpret_cast<SurfaceInteraction*>( unpack_pointer(u0, u1) ); 
}

INLINE DEVICE bool trace_occlusion(
    OptixTraversableHandle handle, float3 ro, float3 rd, float tmin, float tmax
) 
{
    unsigned int occluded = 0u;
    optixTrace(
        handle, 
        ro, 
        rd, 
        tmin, 
        tmax,
        0.0f,
        OptixVisibilityMask(1),
        OPTIX_RAY_FLAG_TERMINATE_ON_FIRST_HIT,
        RAY_TYPE_OCCLUSION,
        RAY_TYPE_COUNT,
        RAY_TYPE_OCCLUSION,
        occluded
    );
    return occluded;
}

INLINE DEVICE void traceRadiance(
    OptixTraversableHandle handle,
    float3                 ray_origin,
    float3                 ray_direction,
    float                  tmin,
    float                  tmax,
    SurfaceInteraction*    si
) 
{
    // TODO: deduce stride from num ray-types passed in params

    unsigned int u0, u1;
    packPointer( si, u0, u1 );
    optixTrace(
        handle,
        ray_origin,
        ray_direction,
        tmin,
        tmax,
        0.0f,                // rayTime
        OptixVisibilityMask( 1 ),
        OPTIX_RAY_FLAG_NONE,
        RAY_TYPE_RADIANCE,        // SBT offset
        RAY_TYPE_COUNT,           // SBT stride
        RAY_TYPE_RADIANCE,        // missSBTIndex
        u0, u1 );
}

}