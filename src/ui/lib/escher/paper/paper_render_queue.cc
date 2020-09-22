// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/paper/paper_render_queue.h"

#include "src/ui/lib/escher/paper/paper_render_queue_context.h"
#include "src/ui/lib/escher/util/trace_macros.h"

namespace escher {

PaperRenderQueue::PaperRenderQueue() = default;
PaperRenderQueue::~PaperRenderQueue() = default;

void PaperRenderQueue::Clear() {
  TRACE_DURATION("gfx", "PaperRenderQueue::Clear");
  opaque_.clear();
  translucent_.clear();
  wireframe_.clear();
}

void PaperRenderQueue::Sort() {
  TRACE_DURATION("gfx", "PaperRenderQueue::Sort");
  opaque_.Sort();
  translucent_.Sort();
  wireframe_.Sort();
}

void PaperRenderQueue::GenerateCommands(CommandBuffer* cmd_buf,
                                        const PaperRenderQueueContext* context,
                                        PaperRenderQueueFlags flags) const {
  FX_DCHECK(context);
  if (flags & PaperRenderQueueFlagBits::kOpaque) {
    TRACE_DURATION("gfx", "PaperRenderQueue::GenerateCommands[opaque]");
    opaque_.GenerateCommands(cmd_buf, nullptr, context);
  }
  if (flags & PaperRenderQueueFlagBits::kTranslucent) {
    TRACE_DURATION("gfx", "PaperRenderQueue::GenerateCommands[translucent]");
    translucent_.GenerateCommands(cmd_buf, nullptr, context);
  }
  if (flags & PaperRenderQueueFlagBits::kWireframe) {
    TRACE_DURATION("gfx", "PaperRenderQueue::GenerateCommands[wireframe]");
    wireframe_.GenerateCommands(cmd_buf, nullptr, context);
  }
}

void PaperRenderQueue::PushDrawCall(const PaperDrawCall& draw_call) {
  TRACE_DURATION("gfx", "PaperRenderQueue::PushDrawCall");

  const auto& flags = draw_call.render_queue_flags;

#ifndef NDEBUG
  PaperRenderQueueFlags kOpaqueAndTranslucent =
      PaperRenderQueueFlagBits::kOpaque | PaperRenderQueueFlagBits::kTranslucent;
  // Can't use the same sort-key for the translucent and opaque queues.
  // TODO(fxbug.dev/7249): How should sort keys be handled in this situation?  This
  // relates to the question of how different shaders are specified; if the
  // solution to that problem is to enqueue multiple draw-calls, then that's
  // probably also a good solution here.  In that case, maybe we don't even
  // need RenderQueueFlags... PushDrawCalls() and GenerateCommands() could both
  // have an explicit queue-id arg.
  FX_DCHECK(kOpaqueAndTranslucent != (flags & kOpaqueAndTranslucent))
      << "cannot push to both opaque and translucent queue.";
#endif

  if (flags & PaperRenderQueueFlagBits::kOpaque) {
    opaque_.Push(draw_call.render_queue_item);
  }
  if (flags & PaperRenderQueueFlagBits::kTranslucent) {
    translucent_.Push(draw_call.render_queue_item);
  }
  if (flags & PaperRenderQueueFlagBits::kWireframe) {
    wireframe_.Push(draw_call.render_queue_item);
  }
}

}  // namespace escher
