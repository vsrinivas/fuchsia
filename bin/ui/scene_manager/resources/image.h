// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_IMAGE_H_
#define GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_IMAGE_H_

#include "garnet/bin/ui/scene_manager/resources/gpu_memory.h"
#include "garnet/bin/ui/scene_manager/resources/host_memory.h"
#include "garnet/bin/ui/scene_manager/resources/image_base.h"
#include "garnet/bin/ui/scene_manager/resources/memory.h"
#include "garnet/bin/ui/scene_manager/resources/resource.h"
#include "lib/escher/vk/image.h"

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

  // Updates pixels before rendering, if needed. Returns true if contents were
  // updated.
  virtual bool UpdatePixels() = 0;

  const escher::ImagePtr& GetEscherImage() override { return image_; }

 protected:
  Image(Session* session,
        scenic::ResourceId id,
        const ResourceTypeInfo& type_info);

  // GPU memory-backed image.
  escher::ImagePtr image_;
};

}  // namespace scene_manager

#endif  // GARNET_BIN_UI_SCENE_MANAGER_RESOURCES_IMAGE_H_
