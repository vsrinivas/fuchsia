// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_H_

#include "src/ui/lib/escher/vk/image.h"
#include "src/ui/scenic/lib/gfx/resources/image_base.h"
#include "src/ui/scenic/lib/gfx/resources/memory.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

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
                      const fuchsia::images::ImageInfo& image_info, uint64_t memory_offset,
                      ErrorReporter* error_reporter);

  static ImagePtr New(Session* session, ResourceId id, uint32_t width, uint32_t height,
                      uint32_t buffer_collection_id, uint32_t buffer_collection_index,
                      ErrorReporter* error_reporter);

  void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader,
                         escher::ImageLayoutUpdater* layout_uploader) override;

  const escher::ImagePtr& GetEscherImage() override;
  bool use_protected_memory() override { return image_->use_protected_memory(); }

  // TODO(fxbug.dev/24223): Determine proper signaling for marking images as dirty.
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

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_IMAGE_H_
