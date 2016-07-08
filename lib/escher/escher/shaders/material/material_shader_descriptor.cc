// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/shaders/material/material_shader_factory.h"

namespace escher {

MaterialShaderDescriptor::MaterialShaderDescriptor(
    BindingType color_binding_type,
    Displacement::Type displacement,
    Modifier::Mask mask,
    bool has_texture)
    : color_binding_type(color_binding_type),
      displacement(displacement),
      mask(mask),
      has_texture(has_texture) {}

MaterialShaderDescriptor::~MaterialShaderDescriptor() {}

bool MaterialShaderDescriptor::operator==(
    const MaterialShaderDescriptor& other) const {
  return has_texture == other.has_texture &&
         color_binding_type == other.color_binding_type &&
         displacement == other.displacement && mask == other.mask;
}

size_t MaterialShaderDescriptor::GetHashCode() const {
  return (has_texture ? 1 : 0) +
         static_cast<int>(color_binding_type) * 37 +
         static_cast<int>(displacement) * 37 * 37 + static_cast<int>(mask);
}

}  // namespace escher
