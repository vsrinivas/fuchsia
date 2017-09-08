// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/scenic/skia/host_surfaces.h"

#include "apps/mozart/lib/scenic/skia/image_info.h"
#include "lib/ftl/logging.h"

namespace scenic_lib {
namespace skia {
namespace {
void ReleaseData(void* pixels, void* context) {
  static_cast<HostData*>(context)->Release();
}
}  // namespace

sk_sp<SkSurface> MakeSkSurface(const HostImage& image) {
  return MakeSkSurface(image.info(), image.data(), image.memory_offset());
}

sk_sp<SkSurface> MakeSkSurface(const scenic::ImageInfo& image_info,
                               ftl::RefPtr<HostData> data,
                               off_t memory_offset) {
  return MakeSkSurface(MakeSkImageInfo(image_info), image_info.stride,
                       std::move(data), memory_offset);
}

sk_sp<SkSurface> MakeSkSurface(SkImageInfo image_info,
                               size_t row_bytes,
                               ftl::RefPtr<HostData> data,
                               off_t memory_offset) {
  data->AddRef();
  return SkSurface::MakeRasterDirectReleaseProc(
      image_info, static_cast<uint8_t*>(data->ptr()) + memory_offset, row_bytes,
      &ReleaseData, data.get());
}

HostSkSurfacePool::HostSkSurfacePool(Session* session, uint32_t num_images)
    : image_pool_(session, num_images), surface_ptrs_(num_images) {}

HostSkSurfacePool::~HostSkSurfacePool() = default;

bool HostSkSurfacePool::Configure(const scenic::ImageInfo* image_info) {
  if (!image_pool_.Configure(std::move(image_info)))
    return false;

  for (uint32_t i = 0; i < num_images(); i++)
    surface_ptrs_[i].reset();
  return true;
}

sk_sp<SkSurface> HostSkSurfacePool::GetSkSurface(uint32_t index) {
  FTL_DCHECK(index < num_images());

  if (surface_ptrs_[index])
    return surface_ptrs_[index];

  const HostImage* image = image_pool_.GetImage(index);
  if (!image)
    return nullptr;

  surface_ptrs_[index] = MakeSkSurface(*image);
  return surface_ptrs_[index];
}

}  // namespace skia
}  // namespace scenic_lib
