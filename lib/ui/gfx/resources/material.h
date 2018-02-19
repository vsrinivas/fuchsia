// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_RESOURCES_MATERIAL_H_
#define GARNET_LIB_UI_GFX_RESOURCES_MATERIAL_H_

#include "garnet/lib/ui/gfx/resources/resource.h"
#include "lib/escher/material/material.h"

namespace scenic {
namespace gfx {

class ImageBase;
using ImageBasePtr = fxl::RefPtr<ImageBase>;

class Material;
using MaterialPtr = fxl::RefPtr<Material>;

class Material : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;

  Material(Session* session, scenic::ResourceId id);

  void SetColor(float red, float green, float blue, float alpha);
  void SetTexture(ImageBasePtr texture_image);

  float red() const { return escher_material_->color().x; }
  float green() const { return escher_material_->color().y; }
  float blue() const { return escher_material_->color().z; }
  float alpha() const { return escher_material_->color().w; }
  const ImageBasePtr& texture_image() const { return texture_; }
  const escher::MaterialPtr& escher_material() const {
    return escher_material_;
  }

  void Accept(class ResourceVisitor* visitor) override;

  // Called at presentation time to allow ImagePipes to update current image.
  void UpdateEscherMaterial();

 private:
  escher::MaterialPtr escher_material_;
  ImageBasePtr texture_;
};

}  // namespace gfx
}  // namespace scenic

#endif  // GARNET_LIB_UI_GFX_RESOURCES_MATERIAL_H_
