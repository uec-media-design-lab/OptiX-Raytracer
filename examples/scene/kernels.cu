#include <prayground/prayground.h>
#include "params.h"

extern "C" { __constant__ LaunchParams params; }

using SurfaceInteraction = SurfaceInteraction_<Vec3f>;

static __forceinline__ __device__ SurfaceInteraction* getSurfaceInteraction()
{
	const uint32_t u0 = getPayload<0>();
	const uint32_t u1 = getPayload<1>();
	return reinterpret_cast<SurfaceInteraction*>(unpackPointer(u0, u1));
}

static __forceinline__ __device__ void trace(
	OptixTraversableHandle handle, const Vec3f& ro, const Vec3f& rd,
	float tmin, float tmax, uint32_t ray_type, SurfaceInteraction* si)
{
	uint32_t u0, u1;
	packPointer(si, u0, u1);
	optixTrace(
		handle, ro, rd,
		tmin, tmax, 0,
		OptixVisibilityMask(1), OPTIX_RAY_FLAG_NONE,
		ray_type, 2, ray_type,
		u0, u1
	);
}

// raygen
extern "C" __device__ void __raygen__pinhole()
{
	const pgRaygenData<Camera>* raygen = (pgRaygenData<Camera>*)optixGetSbtDataPointer();

	const int frame = params.frame;

	const Vec3ui idx(optixGetLaunchIndex());
	uint32_t seed = tea<4>(idx.y() * params.width + idx.x(), frame);

	Vec3f result(0.0f);
	Vec3f normal(0.0f);

	int i = params.samples_per_launch;

	while (i > 0)
	{
		const Vec2f jitter = UniformSampler::get2D(seed) - 0.5f;
		const Vec2f d = 2.0f * Vec2f(
			(static_cast<float>(idx.x()) + jitter.x()) / params.width,
			(static_cast<float>(idx.y()) + jitter.y()) / params.height
		) - 1.0f;

		Vec3f ro, rd;
		getCameraRay(raygen->camera, d.x(), d.y(), ro, rd);

		Vec3f throughput(1.0f);

		SurfaceInteraction si;
		si.seed = seed;
		si.emission = 0.0f;
		si.albedo = 0.0f;
		si.trace_terminate = false;

		int depth = 0;
		for (;;)
		{
            if ( depth >= params.max_depth )
				break;

            trace(params.handle, ro, rd, 0.01f, 1e16f, /* ray_type = */ 0, &si);

            if (si.trace_terminate) {
                result += si.emission * throughput;
                break;
            }

            if (depth == 0)
                normal = si.shading.n;

            // Get emission from area emitter
            if ( si.surface_info.type == SurfaceType::AreaEmitter )
            {
                // Evaluating emission from emitter
                optixDirectCall<void, SurfaceInteraction*, void*>(
                    si.surface_info.callable_id.bsdf, &si, si.surface_info.data);

                result += si.emission * throughput;
                if (si.trace_terminate)
                    break;
            }
            // Specular sampling
            else if (+(si.surface_info.type & SurfaceType::Delta))
            {
                // Sampling scattered direction
                optixDirectCall<void, SurfaceInteraction*, void*>(
                    si.surface_info.callable_id.sample, &si, si.surface_info.data);
                
                // Evaluate bsdf
                Vec3f bsdf_val = optixContinuationCall<Vec3f, SurfaceInteraction*, void*>(
                    si.surface_info.callable_id.bsdf, &si, si.surface_info.data);
                throughput *= bsdf_val;
            }
            // Rough surface sampling with applying MIS
            else if ( +(si.surface_info.type & (SurfaceType::Rough | SurfaceType::Diffuse)) )
            {
                // Importance sampling according to the BSDF
                optixDirectCall<void, SurfaceInteraction*, void*>(
                    si.surface_info.callable_id.sample, &si, si.surface_info.data);

                // Evaluate PDF of area emitter
                float pdf = optixDirectCall<float, SurfaceInteraction*, void*>(
                    si.surface_info.callable_id.pdf, &si, si.surface_info.data);

                // Evaluate BSDF
                Vec3f bsdf = optixContinuationCall<Vec3f, SurfaceInteraction*, void*>(
                    si.surface_info.callable_id.bsdf, &si, si.surface_info.data);

                throughput *= bsdf / pdf;
            }
            
            ro = si.p;
            rd = si.wi;

            ++depth;
		}
        i--;
	}

    const uint32_t image_idx = idx.y() * params.width + idx.x();

    if (result.x() != result.x()) result.x() = 0.0f;
    if (result.y() != result.y()) result.y() = 0.0f;
    if (result.z() != result.z()) result.z() = 0.0f;

    Vec3f accum_color = result / static_cast<float>(params.samples_per_launch);

    if (frame > 0)
    {
        const float a = 1.0f / static_cast<float>(frame + 1);
        const Vec3f accum_color_prev(params.accum_buffer[image_idx]);
        accum_color = lerp(accum_color_prev, accum_color, a);
    }
    params.accum_buffer[image_idx] = Vec4f(accum_color, 1.0f);
    Vec3u color = make_color(accum_color);
    params.result_buffer[image_idx] = Vec4u(color, 255);
}

// Miss
extern "C" __device__ void __miss__envmap()
{
    pgMissData* data = reinterpret_cast<pgMissData*>(optixGetSbtDataPointer());
    auto* env = reinterpret_cast<EnvironmentEmitter::Data*>(data->env_data);
    SurfaceInteraction* si = getSurfaceInteraction();

    Ray ray = getWorldRay();

    const float a = dot(ray.d, ray.d);
    const float half_b = dot(ray.o, ray.d);
    const float c = dot(ray.o, ray.o) - 1e8f * 1e8f;
    const float discriminant = half_b * half_b - a * c;

    float sqrtd = sqrtf(discriminant);
    float t = (-half_b + sqrtd) / a;

    Vec3f p = normalize(ray.at(t));

    float phi = atan2(p.z(), p.x());
    float theta = asin(p.y());
    float u = 1.0f - (phi + math::pi) / (2.0f * math::pi);
    float v = 1.0f - (theta + math::pi / 2.0f) * math::inv_pi;
    si->shading.uv = Vec2f(u, v);
    si->trace_terminate = true;
    si->surface_info.type = SurfaceType::None;
    si->emission = optixDirectCall<Vec3f, SurfaceInteraction*, void*>(
        env->texture.prg_id, si, env->texture.data);
}

extern "C" __device__ void __miss__shadow()
{
    setPayload<0>(1);
}

// Hitgroups 
extern "C" __device__ void __closesthit__custom()
{
    pgHitgroupData* data = reinterpret_cast<pgHitgroupData*>(optixGetSbtDataPointer());

    Ray ray = getWorldRay();

    // If you use the __intersection__pg_*() function for intersection test, 
    // you can fetch shading frame from two attributes
    Shading* shading = getPtrFromTwoAttributes<Shading, 0>();

    // Transform shading frame to world space
    shading->n = optixTransformNormalFromObjectToWorldSpace(shading->n);
    shading->dpdu = optixTransformVectorFromObjectToWorldSpace(shading->dpdu);
    shading->dpdv = optixTransformVectorFromObjectToWorldSpace(shading->dpdv);

    auto* si = getSurfaceInteraction();

    si->p = ray.at(ray.tmax);
    si->shading = *shading;
    si->t = ray.tmax;
    si->wo = ray.d;
    si->surface_info = data->surface_info;
}

extern "C" __device__ void __closesthit__mesh()
{
    pgHitgroupData* data = reinterpret_cast<pgHitgroupData*>(optixGetSbtDataPointer());
    const TriangleMesh::Data* mesh_data = reinterpret_cast<TriangleMesh::Data*>(data->shape_data);

    Ray ray = getWorldRay();

    const int prim_id = optixGetPrimitiveIndex();
    Shading shading = getMeshShading(mesh_data, optixGetTriangleBarycentrics(), prim_id);
    // Transform shading from object to world space.
    shading.n = optixTransformNormalFromObjectToWorldSpace(shading.n);
    shading.dpdu = optixTransformVectorFromObjectToWorldSpace(shading.dpdu);
    shading.dpdv = optixTransformVectorFromObjectToWorldSpace(shading.dpdv);

    // Store the surface interaction data at the hit point
    SurfaceInteraction* si = getSurfaceInteraction();
    si->p = ray.at(ray.tmax);
    si->shading = shading;
    si->t = ray.tmax;
    si->wo = ray.d;
    si->surface_info = data->surface_info;
}

extern "C" __device__ void __closesthit__curves()
{
    const pgHitgroupData* data = reinterpret_cast<pgHitgroupData*>(optixGetSbtDataPointer());
    const Curves::Data* curves = reinterpret_cast<Curves::Data*>(data->shape_data);

    // Get segment ID
    const uint32_t primitive_id = optixGetPrimitiveIndex();

    Ray ray = getWorldRay();
    Vec3f hit_point = optixTransformPointFromWorldToObjectSpace(ray.at(ray.tmax));

    Shading shading = getCurvesShading(hit_point, primitive_id, optixGetPrimitiveType());
    // Transform shading frame to world space
    shading.n = optixTransformNormalFromObjectToWorldSpace(shading.n);
    shading.dpdu = optixTransformVectorFromObjectToWorldSpace(shading.dpdu);
    shading.dpdv = optixTransformVectorFromObjectToWorldSpace(shading.dpdv);

    SurfaceInteraction* si = getSurfaceInteraction();
    si->p = optixTransformPointFromObjectToWorldSpace(hit_point);
    si->shading = shading;
    si->t = ray.tmax;
    si->wo = ray.d;
    si->surface_info = data->surface_info;
}

extern "C" __device__ void __closesthit__shadow()
{
    setPayload<0>(0);
}

// Surfaces
// Diffuse -----------------------------------------------------------------------------------------------
extern "C" __device__ void __direct_callable__sample_diffuse(SurfaceInteraction * si, void* mat_data) {
    const Diffuse::Data* diffuse = reinterpret_cast<Diffuse::Data*>(mat_data);
    si->wi = importanceSamplingDiffuse(diffuse, si->wo, si->shading, si->seed);
}

extern "C" __device__ Vec3f __continuation_callable__bsdf_diffuse(SurfaceInteraction * si, void* mat_data)
{
    const Diffuse::Data* diffuse = reinterpret_cast<Diffuse::Data*>(mat_data);
    const Vec3f albedo = optixDirectCall<Vec3f, SurfaceInteraction*, void*>(
        diffuse->texture.prg_id, si, diffuse->texture.data);
    si->albedo = albedo;
    si->emission = Vec3f(0.0f);
    return albedo * getDiffuseBRDF(si->wi, si->shading.n);
}

extern "C" __device__ float __direct_callable__pdf_diffuse(SurfaceInteraction * si, void* mat_data)
{
    return getDiffusePDF(si->wi, si->shading.n);
}

// Dielectric --------------------------------------------------------------------------------------------
extern "C" __device__ void __direct_callable__sample_glass(SurfaceInteraction * si, void* mat_data) {
    const Dielectric::Data* dielectric = reinterpret_cast<Dielectric::Data*>(mat_data);

    float ni = 1.000292f; // air
    float nt = dielectric->ior;  // ior specified 
    float cosine = dot(si->wo, si->shading.n);
    bool into = cosine < 0;
    Vec3f outward_normal = into ? si->shading.n : -si->shading.n;

    if (!into) swap(ni, nt);

    cosine = fabs(cosine);
    float sine = sqrtf(1.0 - cosine * cosine);
    bool cannot_refract = (ni / nt) * sine > 1.0f;

    float reflect_prob = fresnel(cosine, ni, nt);
    unsigned int seed = si->seed;

    if (cannot_refract || reflect_prob > rnd(seed))
        si->wi = reflect(si->wo, outward_normal);
    else
        si->wi = refract(si->wo, outward_normal, cosine, ni, nt);
    si->trace_terminate = false;
    si->seed = seed;
}

extern "C" __device__ Vec3f __continuation_callable__bsdf_glass(SurfaceInteraction * si, void* mat_data)
{
    const Dielectric::Data* dielectric = reinterpret_cast<Dielectric::Data*>(mat_data);
    si->emission = Vec3f(0.0f);
    Vec3f albedo = optixDirectCall<Vec3f, SurfaceInteraction*, void*>(
        dielectric->texture.prg_id, si, dielectric->texture.data);
    si->albedo = albedo;
    return albedo;
}

extern "C" __device__ float __direct_callable__pdf_glass(SurfaceInteraction * si, void* mat_data)
{
    return 1.0f;
}

// Area emitter ------------------------------------------------------------------------------------------
extern "C" __device__ void __direct_callable__area_emitter(SurfaceInteraction * si, void* surface_data)
{
    const auto* area = reinterpret_cast<AreaEmitter::Data*>(surface_data);
    si->trace_terminate = true;
    float is_emitted = dot(si->wo, si->shading.n) < 0.0f ? 1.0f : 0.0f;
    if (area->twosided)
    {
        is_emitted = 1.0f;
        si->shading.n = faceforward(si->shading.n, -si->wo, si->shading.n);
    }

    const Vec3f base = optixDirectCall<Vec3f, SurfaceInteraction*, void*>(
        area->texture.prg_id, si, area->texture.data);
    si->albedo = base;

    si->emission = base * area->intensity * is_emitted;
}

// Textures
extern "C" __device__ Vec3f __direct_callable__bitmap(SurfaceInteraction * si, void* tex_data) {
    const auto* image = reinterpret_cast<BitmapTexture::Data*>(tex_data);
    float4 c = tex2D<float4>(image->texture, si->shading.uv.x(), si->shading.uv.y());
    return Vec3f(c);
}

extern "C" __device__ Vec3f __direct_callable__constant(SurfaceInteraction * si, void* tex_data) {
    const auto* constant = reinterpret_cast<ConstantTexture::Data*>(tex_data);
    return constant->color;
}

extern "C" __device__ Vec3f __direct_callable__checker(SurfaceInteraction * si, void* tex_data) {
    const auto* checker = reinterpret_cast<CheckerTexture::Data*>(tex_data);
    const bool is_odd = sinf(si->shading.uv.x() * math::pi * checker->scale) * sinf(si->shading.uv.y() * math::pi * checker->scale) < 0;
    return lerp(checker->color1, checker->color2, (float)is_odd);
}