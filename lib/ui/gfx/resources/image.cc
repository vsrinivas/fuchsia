// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/resources/image.h"

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/gpu_image.h"
#include "garnet/lib/ui/gfx/resources/gpu_memory.h"
#include "garnet/lib/ui/gfx/resources/host_image.h"
#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "lib/escher/util/image_utils.h"

namespace scenic {
namespace gfx {

const ResourceTypeInfo Image::kTypeInfo = {
    ResourceType::kImage | ResourceType::kImageBase, "Image"};

Image::Image(Session* session, scenic::ResourceId id,
             const ResourceTypeInfo& type_info)
    : ImageBase(session, id, Image::kTypeInfo) {
  FXL_DCHECK(type_info.IsKindOf(Image::kTypeInfo));
}

ImagePtr Image::New(Session* session, scenic::ResourceId id, MemoryPtr memory,
                    const fuchsia::images::ImageInfo& image_info,
                    uint64_t memory_offset, ErrorReporter* error_reporter) {
  // Create from host memory.
  if (memory->IsKindOf<HostMemory>()) {
    return HostImage::New(session, id, memory->As<HostMemory>(), image_info,
                          memory_offset, error_reporter);

    // Create from GPU memory.
  } else if (memory->IsKindOf<GpuMemory>()) {
    return GpuImage::New(session, id, memory->As<GpuMemory>(), image_info,
                         memory_offset, error_reporter);
  } else {
    FXL_CHECK(false);
    return nullptr;
  }
}

}  // namespace gfx
}  // namespace scenic
