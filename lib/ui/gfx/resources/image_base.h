// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_IMAGE_BASE_H_
#define GARNET_LIB_UI_GFX_RESOURCES_IMAGE_BASE_H_

#include "garnet/lib/ui/gfx/engine/session.h"
#include "garnet/lib/ui/gfx/resources/resource.h"

namespace escher {
class BatchGpuUploader;
}

namespace scenic_impl {
namespace gfx {

// Abstract superclass for Image and ImagePipe.
class ImageBase : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  // Updates the Escher image to the latest image. No-op if the image is not
  // dirty. To get the latest image, this must be called before
  // GetEscherImage().
  virtual void UpdateEscherImage(escher::BatchGpuUploader* gpu_uploader) = 0;

  // Returns the image that should currently be presented. Can be null.
  virtual const escher::ImagePtr& GetEscherImage() = 0;

 protected:
  ImageBase(Session* session, ResourceId id, const ResourceTypeInfo& type_info);
};

using ImageBasePtr = fxl::RefPtr<ImageBase>;

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_RESOURCES_IMAGE_BASE_H_
