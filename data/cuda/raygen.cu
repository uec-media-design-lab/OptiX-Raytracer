#include <optix.h>
#include <cuda/random.h>
#include <sutil/vec_math.h>
#include "../../oprt/core/color.h"
#include "../../oprt/oprt.h"
#include "../../oprt/params.h"

namespace oprt 
{
    
static __forceinline__ __device__ cameraUVW(const CameraData& camera, float3& U, float3& V, float3& W)
{
    W = camera.lookat - camera.eye;
    float wlen = length(W);
    U = normalize(cross(W, camera.up));
    V = normalize(cross(U, W));

    float vlen = wlen * tanf(0.5f * camera.fov * M_PIf / 180.0f);
    V *= vlen;
    float ulen = vlen * camera.aspect;
    U *= ulen;
}

extern "C" __device__ __raygen__pinhole()
{
    const RaygenData* raygen = reinterpret_cast<RaygenData*>(optixGetSbtDataPointer());
    float3 U, V, W;
    cameraUVW(raygen.camera, U, V, W);

    const uint3 idx = optixGetLaunchIndex();
    unsigned int seed = tea<4>(idx.y * params.width + idx.x, subframe_index);

    float3 result = make_float3(0.0f, 0.0f, 0.0f);
    int i = params.samples_per_launch;

    do
	{
		const float2 subpixel_jitter = make_float2(rnd(seed) - 0.5f, rnd(seed) - 0.5f);

		const float2 d = 2.0f * make_float2(
			(static_cast<float>(idx.x) + subpixel_jitter.x) / static_cast<float>(params.width),
			(static_cast<float>(idx.y) + subpixel_jitter.y) / static_cast<float>(params.height)
		) - 1.0f;
		float3 rd = normalize(d.x * U + d.y * V + W);
		float3 ro = raygen.camera.origin;

		float3 attenuation = make_float3(1.0f);

		SurfaceInteraction si;
		si.seed = seed;
		si.emission = make_float3(0.0f);
		si.trace_terminate = false;
		si.radiance_evaled = false;

		int depth = 0;
		for ( ;; ) {

			if ( depth >= params.max_depth )
				break;

			trace(
				params.handle,
				ray_origin, 
				ray_direction, 
				0.01f, 
				1e16f, 
				&si 
			);

			if ( si.trace_terminate )
			{
				result += si.emission * attenuation;
				break;
			}

			if ( si.surface_type == SurfaceType::Emitter )
			{
				// Evaluating emission from emitter
				optixDirectCall<void, SurfaceInteraction*, void*>(
					si.surface_property.func_base_id, 
					&si, 
					si.surface_property.data
				);
				result += si.emission * attenuation;
				if (si.trace_terminate)
					break;
			}
			else if ( si.surface_type == SurfaceType::Material )
			{
				// Sampling scattered direction
				optixDirectCall<void, SurfaceInteraction*, void*>(
					si.surface_property.func_base_id, 
					&si,
					si.surface_property.data
				);

				// Evaluate bsdf 
				float3 bsdf_val = optixContinuationCall<float3, SurfaceInteraction*, void*>(
					si.surface_property.func_base_id, 
					&si,
					si.surface_property.data
				);
				
				// Evaluate pdf
				float pdf_val = optixDirectCall<float, SurfaceInteraction*, void*>(
					si.surface_property.func_base_id + 1, 
					&si,  
					si.surface_property.data
				);
				
				// attenuation += si.emission;
				attenuation *= (bsdf_val * pdf_val) / pdf_val;
				result += si.emission * attenuation;
			}
			
			ray_origin = si.p;
			ray_direction = si.wo;

			++depth;
		}
	} while (--i);

	const uint3 launch_index = optixGetLaunchIndex();
	const unsigned int image_index = launch_index.y * params.width + launch_index.x;

	if (result.x != result.x) result.x = 0.0f;
	if (result.y != result.y) result.y = 0.0f;
	if (result.z != result.z) result.z = 0.0f;

	float3 accum_color = result / static_cast<float>(params.samples_per_launch);

	if (subframe_index > 0)
	{
		const float a = 1.0f / static_cast<float>(subframe_index + 1);
		const float3 accum_color_prev = make_float3(params.accum_buffer[image_index]);
		accum_color = lerp(accum_color_prev, accum_color, a);
	}
	params.accum_buffer[image_index] = make_float4(accum_color, 1.0f);
	uchar3 color = make_color(accum_color);
	params.frame_buffer[image_index] = make_uchar4(color.x, color.y, color.z, 255);
}

}