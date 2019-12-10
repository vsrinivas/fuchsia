// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_MATERIAL_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_MATERIAL_H_

#include "src/ui/lib/escher/material/material.h"
#include "src/ui/scenic/lib/gfx/resources/resource.h"

namespace escher {
class BatchGpuUploader;
}  // namespace escher

namespace scenic_impl {
namespace gfx {

class ImageBase;
using ImageBasePtr = fxl::RefPtr<ImageBase>;

class Material;
using MaterialPtr = fxl::RefPtr<Material>;

class Material : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Material(Session* session, ResourceId id);

  void SetColor(float red, float green, float blue, float alpha);
  void SetTexture(ImageBasePtr texture_image);

  float red() const { return escher_material_->color().x; }
  float green() const { return escher_material_->color().y; }
  float blue() const { return escher_material_->color().z; }
  float alpha() const { return escher_material_->color().w; }
  const ImageBasePtr& texture_image() const { return texture_; }
  const escher::MaterialPtr& escher_material() const { return escher_material_; }

  void Accept(class ResourceVisitor* visitor) override;

  // Called at presentation time to allow Image(Pipes) to update current image.
  void UpdateEscherMaterial(escher::BatchGpuUploader* gpu_uploader,
                            escher::ImageLayoutUpdater* layout_updater);

 private:
  escher::MaterialPtr escher_material_;
  ImageBasePtr texture_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_MATERIAL_H_
