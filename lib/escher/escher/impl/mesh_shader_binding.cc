// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/impl/mesh_shader_binding.h"

#include "escher/impl/mesh_manager.h"
#include "escher/vk/buffer.h"

namespace escher {
namespace impl {

MeshShaderBinding::MeshShaderBinding(
    vk::VertexInputBindingDescription binding,
    std::vector<vk::VertexInputAttributeDescription> attributes)
    : binding_(std::move(binding)), attributes_(std::move(attributes)) {
  FTL_DCHECK(binding.binding == kTheOnlyCurrentlySupportedBinding);
}

}  // namespace impl
}  // namespace escher
