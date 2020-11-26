// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_render_queue_context.h"

namespace escher {

PaperRenderQueueContext::PaperRenderQueueContext() {
  // TODO(fxbug.dev/65246): once we can use C++20, initialize these where they're declared in
  // RenderQueueContext.
  reserved = 0U;
  client_data = 0U;
}

PaperRenderQueueContext::~PaperRenderQueueContext() = default;

}  // namespace escher
