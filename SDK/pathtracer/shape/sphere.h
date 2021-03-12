#pragma once 

#include <core/shape.h>

namespace pt {

#if !defined(__CUDACC__)
class Sphere : public Shape {
public:
    explicit Sphere(float3 c, float r) : center(c), radius(r) {}

    ShapeType type() const override { return ShapeType::Sphere; }
private:
    float3 center;
    float radius;
};
#endif

}