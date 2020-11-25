// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_RENDERER_RENDER_QUEUE_CONTEXT_H_
#define SRC_UI_LIB_ESCHER_RENDERER_RENDER_QUEUE_CONTEXT_H_

#include "src/ui/lib/escher/renderer/render_queue_item.h"

namespace escher {

// RenderQueueContext has two roles:
//   1) It is used by RenderQueue to choose which of a RendererQueueItem's
//      render_queue_funcs to invoke.
//   2) It is passed to each RenderQueueFunc invocation, where it provides
//      domain-specific data in the form of bits to be interpreted by the
//      invoked function.
//
// It is idiomatic for the invoked function to static_cast to a subclass of
// RenderQueueContext which provides more convenient access to the client-data
// bits.
struct RenderQueueContext {
  static constexpr uint8_t kNumReservedBits = 16;
  static constexpr uint8_t kNumClientDataBits = 48;
  static constexpr uint8_t kNumPrivateBits = 64U - kNumClientDataBits;

  // Reserved for future use.
  uint16_t reserved : kNumReservedBits;

  // Bits to be interpreted arbitrarily by the invoked RenderQueueFunc.
  uint64_t client_data : kNumClientDataBits;

  static_assert(64U == kNumPrivateBits + kNumClientDataBits, "wrong number of bits");
  static_assert(kNumPrivateBits == kNumReservedBits, "wrong number of bits");
};
static_assert(8U == sizeof(RenderQueueContext), "size mismatch");

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_RENDERER_RENDER_QUEUE_CONTEXT_H_
