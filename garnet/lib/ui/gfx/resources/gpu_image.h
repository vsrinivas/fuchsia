// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_GPU_IMAGE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_GPU_IMAGE_H_

#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "src/ui/lib/escher/vk/image.h"

namespace scenic_impl {
namespace gfx {

class GpuImage;
using GpuImagePtr = fxl::RefPtr<GpuImage>;

class GpuImage : public Image {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Create Image given a MemoryPtr, fuchsia::images::ImageInfoPtr, and
  // memory_offset.
  //
  // |session| is the Session that this image can be referenced from.
  // |id| is the ID of the resource.
  // |memory| is the memory that is associated with this image.
  // |image_info| specifies size, format, and other properties.
  // |memory_offset| specifies the offset into |memory| where the image is
  // stored.
  // |error_reporter| is used to log any errors so they can be seen by
  // the caller.
  //
  // Returns the created Image, or nullptr if there was an error.
  static GpuImagePtr New(Session* session, ResourceId id, MemoryPtr memory,
                         const fuchsia::images::ImageInfo& image_info,
                         uint64_t memory_offset, ErrorReporter* error_reporter);

  void Accept(class ResourceVisitor* visitor) override;

 protected:
  // No-op for images backed by GPU memory.
  bool UpdatePixels(escher::BatchGpuUploader* uploader) override;

 private:
  // Create an Image object from a VkImage.
  // |session| is the Session that this image can be referenced from.
  // |id| is the ID of the resource.
  // |gpu_mem| is the GPU memory that is associated with this image.
  // |image_info| specifies size, format, and other properties.
  // |vk_image| is the VkImage, whose lifetime is now controlled by this
  // object.
  GpuImage(Session* session, ResourceId id, escher::GpuMemPtr gpu_mem,
           escher::ImageInfo image_info, vk::Image vk_image);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_GPU_IMAGE_H_
