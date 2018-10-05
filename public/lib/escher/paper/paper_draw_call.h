// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_PAPER_PAPER_DRAW_CALL_H_
#define LIB_ESCHER_PAPER_PAPER_DRAW_CALL_H_

#include "lib/escher/paper/paper_readme.h"

#include "lib/escher/paper/paper_render_queue_flags.h"
#include "lib/escher/renderer/render_queue_item.h"

namespace escher {

// Produced by PaperDrawCallFactory and consumed by PaperRenderQueue::Push().
struct PaperDrawCall {
  RenderQueueItem render_queue_item;
  PaperRenderQueueFlags render_queue_flags;
};

}  // namespace escher

#endif  // LIB_ESCHER_PAPER_PAPER_DRAW_CALL_H_
