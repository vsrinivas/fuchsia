// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_H_
#define SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_H_

#include "src/ui/lib/escher/paper/paper_draw_call.h"
#include "src/ui/lib/escher/paper/paper_readme.h"
#include "src/ui/lib/escher/renderer/render_queue.h"
#include "src/ui/lib/escher/third_party/granite/vk/command_buffer.h"

namespace escher {

// Supports rendering of escher::Model and escher::Objects.  Encapsulates two
// RenderQueues, one for opaque and one for translucent objects.
class PaperRenderQueue final {
 public:
  PaperRenderQueue();
  ~PaperRenderQueue();

  // Push the encapsulated RenderQueueItem onto one or more of the internal
  // queues, as indicated by |draw_call.render_queue_flags|.
  void PushDrawCall(const PaperDrawCall& draw_call);

  // Sort the opaque/translucent RenderQueues.
  void Sort();

  // Set the CommandBuffer state for opaque rendering and invoke
  // GenerateCommands() on the opaque RenderQueue.  Then, set the
  // CommandBuffer state for translucent rendering and invoke GenerateCommands()
  // on the translucent RenderQueue.
  void GenerateCommands(CommandBuffer* cmd_buf, const PaperRenderQueueContext* context,
                        PaperRenderQueueFlags flags) const;

  // Clear per-frame data, as well as the opaque/translucent RenderQueues.
  void Clear();

 private:
  RenderQueue opaque_;
  RenderQueue translucent_;
  RenderQueue wireframe_;
  RenderQueue shadow_caster_;
};

}  // namespace escher

#endif  // SRC_UI_LIB_ESCHER_PAPER_PAPER_RENDER_QUEUE_H_
