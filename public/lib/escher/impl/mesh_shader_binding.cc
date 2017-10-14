// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/mesh_shader_binding.h"

#include "lib/escher/impl/mesh_manager.h"
#include "lib/escher/vk/buffer.h"

namespace escher {
namespace impl {

MeshShaderBinding::MeshShaderBinding(
    vk::VertexInputBindingDescription binding,
    std::vector<vk::VertexInputAttributeDescription> attributes)
    : binding_(std::move(binding)), attributes_(std::move(attributes)) {
  FXL_DCHECK(binding.binding == kTheOnlyCurrentlySupportedBinding);
}

}  // namespace impl
}  // namespace escher
