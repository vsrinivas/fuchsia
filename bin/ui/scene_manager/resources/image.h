// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/gpu_memory.h"
#include "garnet/bin/ui/scene_manager/resources/host_memory.h"
#include "garnet/bin/ui/scene_manager/resources/image_base.h"
#include "garnet/bin/ui/scene_manager/resources/memory.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "escher/renderer/image.h"

namespace scene_manager {

class Image;
using ImagePtr = fxl::RefPtr<Image>;

class Image : public ImageBase {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Create Image given a MemoryPtr, scenic::ImageInfoPtr, and memory_offset.
  //
  // |session| is the Session that this image can be referenced from.
  // |memory| is the memory that is associated with this image.
  // |args| specifies size, format, and other properties.
  // |error_reporter| is used to log any errors so they can be seen by the
  // caller.
  //
  // Returns the created Image, or nullptr if there was an error.
  static ImagePtr New(Session* session,
                      scenic::ResourceId id,
                      MemoryPtr memory,
                      const scenic::ImageInfoPtr& image_info,
                      uint64_t memory_offset,
                      ErrorReporter* error_reporter);

  static ImagePtr NewForTesting(Session* session,
                                scenic::ResourceId id,
                                escher::ResourceManager* image_owner,
                                MemoryPtr host_memory);

  void Accept(class ResourceVisitor* visitor) override;

  const escher::ImagePtr& GetEscherImage() override { return image_; }

 private:
  // Create an Image object from a VkImage.
  // |session| is the Session that this image can be referenced from.
  // |image_info| specifies size, format, and other properties.
  // |vk_image| is the VkImage, whose lifetime is now controlled by this
  // object. |memory| is the GPU memory that is associated with this image.
  Image(Session* session,
        scenic::ResourceId id,
        GpuMemoryPtr memory,
        escher::ImageInfo image_info,
        vk::Image vk_image_);

  // Create an Image object from a escher::Image.
  // |session| is the Session that this image can be referenced from.
  // |image| is the escher::Image that is being wrapped.
  // |memory| is the host memory that is associated with this image.
  //
  // TODO: We might not want to hold on to the memory since we're uploading
  // its contents to the GPU and using the uploaded copy.
  Image(Session* session,
        scenic::ResourceId id,
        MemoryPtr memory,
        escher::ImagePtr image);

  MemoryPtr memory_;
  escher::ImagePtr image_;
};

}  // namespace scene_manager
