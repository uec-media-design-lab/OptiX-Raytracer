#pragma once 

#include <memory>
#include <string>
#include <vector>
#include <vector_types.h>

namespace prayground {

template <typename T> struct AttribItem;

class Attributes {
public:
    Attributes();
    
    void addBool(const std::string& name, std::unique_ptr<bool[]> values, int n);
    void addInt(const std::string& name, std::unique_ptr<int[]> values, int n);
    void addFloat(const std::string& name, std::unique_ptr<float[]> values, int n);
    void addFloat2(const std::string& name, std::unique_ptr<float2[]> values, int n);
    void addFloat3(const std::string& name, std::unique_ptr<float3[]> values, int n);
    void addFloat4(const std::string& name, std::unique_ptr<float4[]> values, int n);
    void addString(const std::string& name, std::unique_ptr<std::string[]> values, int n);

    const bool* findBool(const std::string& name, int n);
    const bool& findOneBool(const std::string& name);
    const int* findInt(const std::string& name, int n);
    const int& findOneInt(const std::string& name);
    const float* findFloat(const std::string& name, int n);
    const float& findOneFloat(const std::string& name);
    const float2* findFloat2(const std::string& name, int n);
    const float2& findOneFloat2(const std::string& name);
    const float3* findFloat3(const std::string& name, int n);
    const float3& findOneFloat3(const std::string& name);
    const float4* findFloat4(const std::string& name, int n);
    const float4& findOneFloat4(const std::string& name);
    const std::string& findString(const std::string& name);
private:
    std::vector<std::shared_ptr<AttribItem<bool>>> m_bools;
    std::vector<std::shared_ptr<AttribItem<int>>> m_ints;
    std::vector<std::shared_ptr<AttribItem<float>>> m_floats;
    std::vector<std::shared_ptr<AttribItem<float2>>> m_float2s;
    std::vector<std::shared_ptr<AttribItem<float3>>> m_float3s;
    std::vector<std::shared_ptr<AttribItem<float4>>> m_float4s;
    std::vector<std::shared_ptr<AttribItem<std::string>>> m_strings;
};

template <typename T>
struct AttribItem {
    const std::string name;
    const std::unique_ptr<T[]> values;
    const int n;
};

} // ::prayground