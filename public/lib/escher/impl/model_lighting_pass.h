// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "lib/escher/impl/model_render_pass.h"

namespace escher {
namespace impl {

// ModelLightingPass encapsulates a vk::RenderPass that is configured for the
// final lighting pass (including shadows, either SSDO or shadow maps).
class ModelLightingPass : public ModelRenderPass {
 public:
  // |ModelRenderPass|
  bool UseMaterialTextures() override { return true; }

  // |ModelRenderPass|
  bool OmitFragmentShader() override { return false; }

  ModelLightingPass(ResourceRecycler* recycler,
                    ModelDataPtr model_data,
                    vk::Format color_format,
                    vk::Format depth_format,
                    uint32_t sample_count);
};

}  // namespace impl
}  // namespace escher
