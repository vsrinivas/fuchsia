// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/view_framework/skia/skia_surface_holder.h"

#include "lib/ftl/logging.h"
#include "mojo/public/cpp/system/buffer.h"
#include "third_party/skia/include/core/SkCanvas.h"

namespace mojo {
namespace ui {

SkiaSurfaceHolder::SkiaSurfaceHolder(const mojo::Size& size) {
  size_t row_bytes = size.width * sizeof(uint32_t);
  size_t total_bytes = size.height * row_bytes;

  FTL_CHECK(MOJO_RESULT_OK ==
            mojo::CreateSharedBuffer(nullptr, total_bytes, &buffer_handle_));
  FTL_CHECK(MOJO_RESULT_OK == mojo::MapBuffer(buffer_handle_.get(), 0u,
                                              total_bytes, &buffer_,
                                              MOJO_MAP_BUFFER_FLAG_NONE));

  surface_ = SkSurface::MakeRasterDirect(
      SkImageInfo::Make(size.width, size.height, kBGRA_8888_SkColorType,
                        kPremul_SkAlphaType),
      buffer_, row_bytes);
}

SkiaSurfaceHolder::~SkiaSurfaceHolder() {
  FTL_CHECK(MOJO_RESULT_OK == mojo::UnmapBuffer(buffer_));
}

mojo::gfx::composition::ImagePtr SkiaSurfaceHolder::TakeImage() {
  FTL_DCHECK(buffer_handle_.get().value());

  auto image = mojo::gfx::composition::Image::New();
  image->size = mojo::Size::New();
  image->size->width = surface_->width();
  image->size->height = surface_->height();
  image->stride = image->size->width * sizeof(uint32_t);
  image->pixel_format = mojo::gfx::composition::Image::PixelFormat::B8G8R8A8;
  image->alpha_format =
      mojo::gfx::composition::Image::AlphaFormat::PREMULTIPLIED;
  image->buffer = std::move(buffer_handle_);
  return image;
}

}  // namespace ui
}  // namespace mojo
