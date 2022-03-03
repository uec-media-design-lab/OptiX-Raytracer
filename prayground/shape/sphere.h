#pragma once 

#include <prayground/core/shape.h>

namespace prayground {

class Sphere final : public Shape {
public:
    struct Data {
        float3 center;
        float radius;
    };

#ifndef __CUDACC__
    Sphere();
    Sphere(const float3& c, float r);

    constexpr ShapeType type() override;
    OptixBuildInput createBuildInput() override;

    void copyToDevice() override;

    AABB bound() const override;

    Data getData() const;
private:
    float3 m_center;
    float m_radius;
    CUdeviceptr d_aabb_buffer{ 0 };

#endif
};

}