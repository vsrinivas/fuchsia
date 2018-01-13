// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_GPU_IMAGE_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_GPU_IMAGE_H_

#include "garnet/bin/ui/scene_manager/resources/gpu_memory.h"
#include "garnet/bin/ui/scene_manager/resources/host_memory.h"
#include "garnet/bin/ui/scene_manager/resources/image.h"
#include "garnet/bin/ui/scene_manager/resources/memory.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "lib/escher/vk/image.h"

namespace scene_manager {

class GpuImage;
using GpuImagePtr = fxl::RefPtr<GpuImage>;

// TODO: We're not handling memory offset.!!
class GpuImage : public Image {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Create Image given a MemoryPtr, scenic::ImageInfoPtr, and memory_offset.
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
  static GpuImagePtr New(Session* session,
                         scenic::ResourceId id,
                         GpuMemoryPtr memory,
                         const scenic::ImageInfoPtr& image_info,
                         uint64_t memory_offset,
                         ErrorReporter* error_reporter);

  void Accept(class ResourceVisitor* visitor) override;

  // No-op for images backed by GPU memory.
  bool UpdatePixels() override;

 private:
  // Create an Image object from a VkImage.
  // |session| is the Session that this image can be referenced from.
  // |image_info| specifies size, format, and other properties.
  // |vk_image| is the VkImage, whose lifetime is now controlled by this
  // object. |memory| is the GPU memory that is associated with this image.
  GpuImage(Session* session,
           scenic::ResourceId id,
           GpuMemoryPtr memory,
           escher::ImageInfo image_info,
           vk::Image vk_image_);

  GpuMemoryPtr memory_;
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_GPU_IMAGE_H_
