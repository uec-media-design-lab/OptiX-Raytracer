#pragma once

#include <cuda/random.h>
#include "../core/material.h"
#include "../core/bsdf.h"
#include "../core/onb.h"
#include "../optix/sbt.h"
#include "../texture/constant.h"

namespace oprt {

struct DisneyData {
    void* base_tex;             // base color
    float subsurface;          
    float metallic;
    float specular;
    float specular_tint;
    float roughness;
    float anisotropic;
    float sheen;
    float sheen_tint;
    float clearcoat;
    float clearcoat_gloss;     
    unsigned int tex_func_idx;  
};

#ifndef __CUDACC__

class Disney final : public Material {
public:
    Disney(){}

    Disney(Texture* base, float subsurface, float metallic,
           float specular, float specular_tint,
           float roughness, float anisotropic, 
           float sheen, float sheen_tint,
           float clearcoat, float clearcoat_gloss)
    : m_base(base), m_subsurface(subsurface), m_metallic(metallic),
      m_specular(specular), m_specular_tint(specular_tint),
      m_roughness(roughness), m_anisotropic(anisotropic),
      m_sheen(sheen), m_sheen_tint(sheen_tint),
      m_clearcoat(clearcoat), m_clearcoat_gloss(clearcoat_gloss) {}
    
    ~Disney() {}

    void prepare_data() override {
        m_base->prepare_data();

        DisneyData data = {
            m_base->get_dptr(),
            m_subsurface,
            m_metallic, 
            m_specular, 
            m_specular_tint,
            m_roughness,
            m_anisotropic,
            m_sheen,
            m_sheen_tint,
            m_clearcoat,
            m_clearcoat_gloss
        };

        CUDA_CHECK(cudaMalloc(&d_data, sizeof(DisneyData)));
        CUDA_CHECK(cudaMemcpy(
            d_data,
            &data, sizeof(DisneyData),
            cudaMemcpyHostToDevice
        ));
    }
private:
    Texture* m_base;
    float m_subsurface;
    float m_metallic;
    float m_specular, m_specular_tint;
    float m_roughness;
    float m_anisotropic;
    float m_sheen, m_sheen_tint;
    float m_clearcoat, m_clearcoat_gloss;
};

#else

CALLABLE_FUNC void CC_FUNC(sample_disney)(SurfaceInteraction* si, void* matdata)
{
    const DisneyData* disney = reinterpret_cast<DisneyData*>(matdata);

    unsigned int seed = si->seed;
    si->trace_terminate = false;
}

CALLABLE_FUNC float3 DC_FUNC(bsdf_disney)(SurfaceInteraction* si, void* matdata)
{   
    
}

CALLABLE_FUNC void DC_FUNC(pdf_disney)(SurfaceInteraction* si, void* matdata)
{

}

#endif

}