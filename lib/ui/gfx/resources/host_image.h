// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_HOST_IMAGE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_HOST_IMAGE_H_

#include "garnet/lib/ui/gfx/resources/host_memory.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/gfx/resources/resource.h"
#include "lib/escher/util/image_utils.h"
#include "lib/escher/vk/image.h"

namespace scenic {
namespace gfx {

class HostImage;
using HostImagePtr = fxl::RefPtr<Image>;
using ImageConversionFunction =
    fit::function<void(void*, void*, uint32_t, uint32_t)>;

// An Image whose contents come from host-accessible memory.
class HostImage : public Image {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Create Image given a HostMemoryPtr, fuchsia::images::ImageInfoPtr, and
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
  static ImagePtr New(Session* session, scenic::ResourceId id,
                      HostMemoryPtr memory,
                      const fuchsia::images::ImageInfo& host_image_info,
                      uint64_t memory_offset, ErrorReporter* error_reporter);

  static ImagePtr NewForTesting(Session* session, scenic::ResourceId id,
                                escher::ResourceManager* image_owner,
                                HostMemoryPtr host_memory);

  void Accept(class ResourceVisitor* visitor) override;

  // Re-upload host memory contents to GPU memory. Returns true if contents were
  // updated.
  bool UpdatePixels() override;

 private:
  // Create an Image object from a escher::Image.
  // |session| is the Session that this image can be referenced from.
  // |id| is the ID assigned to the resource.
  // |memory| is the host memory that is associated with this image.
  // |image| is the escher::Image that is being wrapped.
  // |host_memory_offset| specifies the offset into |memory| where the image is
  // stored.
  HostImage(Session* session, scenic::ResourceId id, HostMemoryPtr memory,
            escher::ImagePtr image, uint64_t host_memory_offset,
            fuchsia::images::ImageInfo host_image_format);

  HostMemoryPtr memory_;
  // The offset into |memory_| where the image is stored, in bytes.
  uint64_t memory_offset_;
  // The format of the image stored in host memory.
  fuchsia::images::ImageInfo host_image_format_;
  escher::image_utils::ImageConversionFunction image_conversion_function_ =
      nullptr;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_HOST_IMAGE_H_
