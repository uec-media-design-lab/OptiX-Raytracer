#include "box_medium.h"

namespace prayground {

BoxMedium::BoxMedium()
: m_min(Vec3f(-1.0f)), m_max(Vec3f(1.0f)), m_density(0.001f)
{

}

BoxMedium::BoxMedium(const Vec3f& min, const Vec3f& max, const float density)
: m_min(min), m_max(max), m_density(density)
{

}

constexpr ShapeType BoxMedium::type()
{
    return ShapeType::Custom;
}

void BoxMedium::copyToDevice() 
{
    BoxMediumData data = this->deviceData();
    if (!d_data)
        CUDA_CHECK(cudaMalloc(&d_data, sizeof(BoxMediumData)));
    CUDA_CHECK(cudaMemcpy(
        d_data, &data, sizeof(BoxMediumData), cudaMemcpyHostToDevice
    ));
}

void BoxMedium::free() 
{
    Shape::free();
    cuda_free(d_aabb_buffer);
}

OptixBuildInput BoxMedium::createBuildInput() 
{
    if (d_aabb_buffer) cuda_free(d_aabb_buffer);
    return createSingleCustomBuildInput(d_aabb_buffer, this->bound(), m_sbt_index);
}

AABB BoxMedium::bound() const 
{
    return AABB(m_min, m_max);
}

const Vec3f& BoxMedium::min() const
{
    return m_min;
}

const Vec3f& BoxMedium::max() const
{
    return m_max;
}

BoxMedium::DataType BoxMedium::deviceData() const
{
    BoxMediumData data = 
    {
        .min = m_min,
        .max = m_max,
        .density = m_density
    };
    return data;
}

} // ::prayground