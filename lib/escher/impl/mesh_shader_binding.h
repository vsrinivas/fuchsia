// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <vulkan/vulkan.hpp>

#include "lib/fxl/macros.h"

namespace escher {
namespace impl {

// MeshShaderBinding contains information about how to bind a Mesh's VBO in
// order to render it, and about attribute indices to use when creating a
// pipeline.
class MeshShaderBinding {
 public:
  // TODO: at some point, we may allow other binding values.  This allows us
  // to track all of the places that will need to be changed.
  static constexpr uint32_t kTheOnlyCurrentlySupportedBinding = 0;

  MeshShaderBinding(
      vk::VertexInputBindingDescription binding,
      std::vector<vk::VertexInputAttributeDescription> attributes);

  const vk::VertexInputBindingDescription* binding() const { return &binding_; }
  const std::vector<vk::VertexInputAttributeDescription>& attributes() const {
    return attributes_;
  }

 private:
  vk::VertexInputBindingDescription binding_;
  std::vector<vk::VertexInputAttributeDescription> attributes_;

  // The binding contains raw pointers into the vector of attributes,
  // which would no longer be valid if a MeshShaderBinding is copied, and the
  // original destroyed.
  FXL_DISALLOW_COPY_AND_ASSIGN(MeshShaderBinding);
};

}  // namespace impl
}  // namespace escher
