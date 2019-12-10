// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/image.h"

#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/gpu_image.h"
#include "src/ui/scenic/lib/gfx/resources/host_image.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo Image::kTypeInfo = {ResourceType::kImage | ResourceType::kImageBase,
                                           "Image"};

Image::Image(Session* session, ResourceId id, const ResourceTypeInfo& type_info)
    : ImageBase(session, id, type_info) {
  FXL_DCHECK(type_info.IsKindOf(Image::kTypeInfo));
}

ImagePtr Image::New(Session* session, ResourceId id, MemoryPtr memory,
                    const fuchsia::images::ImageInfo& image_info, uint64_t memory_offset,
                    ErrorReporter* error_reporter) {
  // Create from host memory.
  if (memory->is_host()) {
    return HostImage::New(session, id, memory, image_info, memory_offset, error_reporter);

    // Create from GPU memory.
  } else {
    return GpuImage::New(session, id, memory, image_info, memory_offset, error_reporter);
  }
}

void Image::UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                              escher::ImageLayoutUpdater* layout_updater) {
  if (dirty_) {
    dirty_ = UpdatePixels(gpu_uploader);
  }
}

const escher::ImagePtr& Image::GetEscherImage() {
  static const escher::ImagePtr kNullEscherImage;
  return dirty_ ? kNullEscherImage : image_;
}

}  // namespace gfx
}  // namespace scenic_impl
