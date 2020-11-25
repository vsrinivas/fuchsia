// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_FLAGS_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_FLAGS_H_

#include "src/ui/lib/escher/util/enum_flags.h"

namespace escher {

// Specifies which |PaperRenderQueue| sub-queues a |PaperDrawCall| should
// be enqueued in.  More than one may be specified.
enum class PaperRenderQueueFlagBits : uint8_t {
  kOpaque = 1U << 0,
  kTranslucent = 1U << 1,
  kWireframe = 1U << 2,
  kShadowCaster = 1U << 3,

  kAllFlags = 0x7u,
};
ESCHER_DECLARE_ENUM_FLAGS(PaperRenderQueueFlags, PaperRenderQueueFlagBits);

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_FLAGS_H_
