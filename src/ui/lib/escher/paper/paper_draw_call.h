// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAW_CALL_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAW_CALL_H_

#include "src/ui/lib/escher/paper/paper_readme.h"

#include "src/ui/lib/escher/paper/paper_render_queue_flags.h"
#include "src/ui/lib/escher/renderer/render_queue_item.h"

namespace escher {

// Produced by PaperDrawCallFactory and consumed by PaperRenderQueue::Push().
struct PaperDrawCall {
  RenderQueueItem render_queue_item;
  PaperRenderQueueFlags render_queue_flags;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_DRAW_CALL_H_
