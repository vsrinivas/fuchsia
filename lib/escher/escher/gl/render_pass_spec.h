// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "escher/geometry/types.h"
#include "escher/gl/texture.h"

namespace escher {

struct RenderPassSpec {
  struct Attachment {
    enum class LoadAction { kWhatever, kLoad, kClear };
    enum class StoreAction { kWhatever, kStore, kMultisampleResolve };

    Texture texture;
    LoadAction load_action = LoadAction::kWhatever;
    StoreAction store_action = StoreAction::kWhatever;
  };
  struct ColorAttachment : public Attachment {
    vec4 clear_color = vec4(0.f, 0.f, 0.f, 1.f);
  };
  struct DepthAttachment : public Attachment {
    double clear_depth = 1.0;
  };

  static constexpr size_t kNumColorAttachments = 1;

  ColorAttachment color[kNumColorAttachments];
  DepthAttachment depth;
  // TODO(jjosh): StencilAttachment stencil_attachment;
};

}  // namespace escher
