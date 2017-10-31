// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/impl/model_render_pass.h"

namespace escher {
namespace impl {

// ModelDepthPass encapsulates a vk::RenderPass that is configured for
// depth-only rendering.
class ModelDepthPass : public ModelRenderPass {
 public:
  // |ModelRenderPass|
  bool UseMaterialTextures() override { return false; }

  // |ModelRenderPass|
  bool OmitFragmentShader() override { return true; }

  // TODO: color_format shouldn't be required for depth-only pass.
  ModelDepthPass(ResourceRecycler* recycler,
                 ModelDataPtr model_data,
                 vk::Format color_format,
                 vk::Format depth_format,
                 uint32_t sample_count);
};

}  // namespace impl
}  // namespace escher
