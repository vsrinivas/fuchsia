// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/material.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo Material::kTypeInfo = {ResourceType::kMaterial,
                                              "Material"};

Material::Material(Session* session,
                   float red,
                   float green,
                   float blue,
                   float alpha)
    : Resource(session, Material::kTypeInfo),
      escher_material_(ftl::MakeRefCounted<escher::Material>()) {
  // TODO: need to add alpha into escher material
  escher_material_->set_color(escher::vec3(red, green, blue));
}

}  // namespace composer
}  // namespace mozart
