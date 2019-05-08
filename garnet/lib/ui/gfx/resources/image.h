// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_IMAGE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_IMAGE_H_

#include "garnet/lib/ui/gfx/resources/image_base.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "src/ui/lib/escher/vk/image.h"

namespace scenic_impl {
namespace gfx {

class Image;
using ImagePtr = fxl::RefPtr<Image>;

class Image : public ImageBase {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Create Image given a MemoryPtr, fuchsia::images::ImageInfoPtr, and
  // memory_offset.
  //
  // |session| is the Session that this image can be referenced from.
  // |memory| is the memory that is associated with this image.
  // |args| specifies size, format, and other properties.
  // |error_reporter| is used to log any errors so they can be seen by the
  // caller.
  //
  // Returns the created Image, or nullptr if there was an error.
  static ImagePtr New(Session* session, ResourceId id, MemoryPtr memory,
                      const fuchsia::images::ImageInfo& image_info,
                      uint64_t memory_offset, ErrorReporter* error_reporter);

  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader) override;
  const escher::ImagePtr& GetEscherImage() override;

  // TODO(SCN-1010): Determine proper signaling for marking images as dirty.
  void MarkAsDirty() { dirty_ = true; }

 protected:
  Image(Session* session, ResourceId id, const ResourceTypeInfo& type_info);

  // Updates pixels before rendering, if needed. Returns the new dirty status
  // (i.e. false, if all bits have been updated appropriately, true if the image
  // is still dirty).
  virtual bool UpdatePixels(escher::BatchGpuUploader* gpu_uploader) = 0;

  // GPU memory-backed image.
  escher::ImagePtr image_;
  bool dirty_ = true;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_IMAGE_H_
