#pragma once 

#ifndef __CUDACC__
#include <prayground/core/shape.h>
#endif 

namespace prayground {

struct BoxData 
{
    float3 min;
    float3 max;
};

#ifndef __CUDACC__
class Box final : public Shape {
public:
    using DataType = BoxData;

    Box();
    Box(const float3& min, const float3& max);

    constexpr ShapeType type() override;

    void copyToDevice() override;
    void free() override;

    OptixBuildInput createBuildInput() override;

    AABB bound() const override;

    const float3& min() const;
    const float3& max() const;

    DataType deviceData() const;
private:
    float3 m_min;
    float3 m_max;
    CUdeviceptr d_aabb_buffer{ 0 };
};
#endif // __CUDACC__

} // ::prayground