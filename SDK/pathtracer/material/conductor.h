#pragma once

#include <core/material.h>
#include <core/bsdf.h>

namespace pt {

class Conductor final : public Material {
private:
    float3 albedo;
    float fuzz;

    void setup_on_device() override {
        CUDA_CHECK(cudaMalloc((void**)&d_ptr, sizeof(MaterialPtr)));
        setup_material_on_device<<<1,1>>>((Conductor**)&d_ptr, albedo, fuzz);
        CUDA_SYNC_CHECK();
    }

    void delete_on_device() override {
        delete_material_on_device<<<1,1>>>(d_ptr);
        CUDA_SYNC_CHECK();
    }

public:
    HOSTDEVICE Conductor(float3 a, float f) : albedo(a), fuzz(f) {
        #ifndef __CUDACC__
        setup_on_device();
        #endif
    }
    HOSTDEVICE ~Conductor() {
        #ifndef __CUDACC__
        delete_on_device();
        #endif
    }

    HOSTDEVICE void sample(SurfaceInteraction& si) const override;
    HOSTDEVICE float3 emittance(SurfaceInteraction& si) const override { return make_float3(0.f); }

    HOST MaterialType type() const override { return MaterialType::Conductor; }
};

HOSTDEVICE void Conductor::sample(SurfaceInteraction& si) const {
    si.wo = reflect(si.wi, si.n);
    si.attenuation = albedo;
    si.trace_terminate = false;
}

}



