﻿#pragma once

#include <prayground/core/texture.h>
#include <prayground/core/bitmap.h>

namespace prayground {

template <typename PixelT>
class BitmapTexture_ final : public Texture, public Bitmap_<PixelT> {
public:
    using ColorType = std::conditional_t<std::is_same_v<PixelT, float>, Vec4f, Vec4u>;
    struct Data
    {
        cudaTextureObject_t texture;
    };

#ifndef __CUDACC__
    BitmapTexture_(const std::filesystem::path& filename, int prg_id);
    BitmapTexture_(const std::filesystem::path& filename, cudaTextureDesc desc, int prg_id);

    constexpr TextureType type() override;

    ColorType eval(const Vec2f& texcoord) const;

    void copyToDevice() override;
    void free() override;

    void setTextureDesc(const cudaTextureDesc& desc);
    cudaTextureDesc textureDesc() const;

private:
    cudaTextureDesc m_tex_desc {};
    cudaTextureObject_t d_texture{ 0 };
    cudaArray_t d_array { nullptr };
#endif // __CUDACC__
};

using BitmapTexture = BitmapTexture_<unsigned char>;
using FloatBitmapTexture = BitmapTexture_<float>;

}