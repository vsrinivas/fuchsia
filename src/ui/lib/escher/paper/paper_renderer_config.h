// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDERER_CONFIG_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDERER_CONFIG_H_

#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/util/debug_print.h"
#include "src/ui/lib/escher/vk/shader_variant_args.h"

#include <vulkan/vulkan.hpp>

namespace escher {

// A list of shadow types which may be supported by |PaperRenderer|.  Some
// shadow techniques will not be supported on some target devices.
enum class PaperRendererShadowType {
  kNone = 0,
  kSsdo,
  kShadowMap,
  kMomentShadowMap,
  kShadowVolume,

  // For EnumCount<>().
  kEnumCount
};

ESCHER_DEBUG_PRINTABLE(PaperRendererShadowType);

// Allows clients to configure the behavior of a |PaperRenderer| by calling
// |SetConfig()| at any time except in the middle of a frame.
struct PaperRendererConfig {
  // Choose a shadow algorithm.
  PaperRendererShadowType shadow_type = PaperRendererShadowType::kNone;

  // Multisampling antialiasing (MSAA) sample count: 1, 2, or 4.
  uint8_t msaa_sample_count = 1;

  // Specify how many depth buffers the renderer should round-robin through.
  // TODO(fxbug.dev/7331): this type of transient resource should be provided by a
  // "FrameGraph" which has global knowledge of the entire frame.
  uint8_t num_depth_buffers = 1;

  // Turn on some sort of debug visualization.
  bool debug = false;

  // Blit the current frame number to the output image.
  bool debug_frame_number = false;

  vk::Format depth_stencil_format = vk::Format::eD24UnormS8Uint;
};

ESCHER_DEBUG_PRINTABLE(PaperRendererConfig);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDERER_CONFIG_H_
