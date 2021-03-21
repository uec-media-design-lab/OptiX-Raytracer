#include <include/optix/util.h>
#include "../material/conductor.h"
#include "../material/dielectric.h"
#include "../material/diffuse.h"
#include "../material/emitter.h"


/** 
 * \brief Initialize object on device.
 * 
 * \note Initailization must be excecuted only once.
 */
template <typename T, typename... Args>
__global__ void setup_object(T** d_ptr, Args... args) {
    (*d_ptr) = new T(args...);
}

template <typename T>
__global__ void delete_object(T* d_ptr) {
    delete d_ptr;
}

// Conductor
pt::Conductor::Conductor(float3 albedo, float fuzz) : m_albedo(albedo), m_fuzz(fuzz)
{
    #ifndef __CUDACC__
    setup_on_device();
    #endif
}

pt::Conductor::~Conductor() {
    #ifndef __CUDACC__  
    delete_on_device();
    #endif
}

void pt::Conductor::setup_on_device() {
    cudaMalloc(reinterpret_cast<void**>(&d_ptr), sizeof(Material*));
    setup_object<<<1,1>>>((Conductor**)&d_ptr, m_albedo, m_fuzz);
}

void pt::Conductor::delete_on_device() {
    delete_object<<<1,1>>>(d_ptr);
}

// Dielectric
HOSTDEVICE pt::Dielectric::Dielectric(float3 albedo, float ior) : m_albedo(albedo), m_ior(ior)
{
    #ifndef __CUDACC__
    setup_on_device();
    #endif
}

HOSTDEVICE pt::Dielectric::~Dielectric() {
    #ifndef __CUDACC__  
    delete_on_device();
    #endif
}

void pt::Dielectric::setup_on_device() {
    cudaMalloc(reinterpret_cast<void**>(&d_ptr), sizeof(Material*));
    setup_object<<<1,1>>>((Dielectric**)&d_ptr, m_albedo, m_ior);
}

void pt::Dielectric::delete_on_device() {
    delete_object<<<1,1>>>(d_ptr);
}

// Diffuse
pt::Diffuse::Diffuse(float3 albedo) : m_albedo(albedo)
{
    #ifndef __CUDACC__
    setup_on_device();
    #endif
}

pt::Diffuse::~Diffuse() {
    #ifndef __CUDACC__  
    delete_on_device();
    #endif
}

void pt::Diffuse::setup_on_device() {
    cudaMalloc(reinterpret_cast<void**>(&d_ptr), sizeof(Material*));
    setup_object<<<1,1>>>((Diffuse**)&d_ptr, m_albedo);
}

void pt::Diffuse::delete_on_device() {
    delete_object<<<1,1>>>(d_ptr);
}

// Emitter
pt::Emitter::Emitter(float3 color, float strength) : m_color(color), m_strength(strength)
{
    #ifndef __CUDACC__
    setup_on_device();
    #endif
}

pt::Emitter::~Emitter() {
    #ifndef __CUDACC__  
    delete_on_device();
    #endif
}

void pt::Emitter::setup_on_device() {
    cudaMalloc(reinterpret_cast<void**>(&d_ptr), sizeof(Material*));
    setup_object<<<1,1>>>((Emitter**)&d_ptr, m_color, m_strength);
}

void pt::Emitter::delete_on_device() {
    delete_object<<<1,1>>>(d_ptr);
}


