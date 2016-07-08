// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/material/material_shader_factory.h"

namespace escher {

MaterialShaderFactory::MaterialShaderFactory() {}

MaterialShaderFactory::~MaterialShaderFactory() {}

const MaterialShader* MaterialShaderFactory::GetShader(
    const Material& material,
    const Modifier& modifier) {
  MaterialShaderDescriptor descriptor(material.color().type(),
                                      material.displacement().type(),
                                      modifier.mask(),
                                      material.has_texture());

  auto it = shaders_.find(descriptor);
  if (it == shaders_.end()) {
    std::unique_ptr<MaterialShader> shader(new MaterialShader(descriptor));
    if (!shader->Compile())
      return nullptr;
    it = shaders_.emplace(descriptor, std::move(shader)).first;
  }
  return it->second.get();
}

}  // namespace escher
