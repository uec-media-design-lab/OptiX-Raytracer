#pragma once

#include "../core/util.h"
#include "../core/texture.h"
#include "../core/emitter.h"
#include "../texture/constant.h"
#include "cuda/area.cuh"

namespace oprt {

#ifndef __CUDACC__

class AreaEmitter final : public Emitter {
public:
    AreaEmitter(const float3& color, float intensity=1.0f, bool twosided=true);
    AreaEmitter(const std::shared_ptr<Texture>& texture, float intensity = 1.0f, bool twosided = true);

    void prepareData() override;
    void freeData() override;

    EmitterType type() const override { return EmitterType::Area; }
private:
    std::shared_ptr<Texture> m_texture;
    float m_intensity;
    bool m_twosided;
};

#endif // __CUDACC__

}