// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/renderer/render_queue.h"

#include "src/ui/lib/escher/renderer/render_queue_context.h"

namespace escher {

RenderQueue::RenderQueue() = default;
RenderQueue::~RenderQueue() = default;

void RenderQueue::Sort() {
  std::stable_sort(
      items_.begin(), items_.end(),
      [](const RenderQueueItem& a, const RenderQueueItem& b) { return a.sort_key < b.sort_key; });
}

void RenderQueue::GenerateCommands(CommandBuffer* cmd_buf, const CommandBuffer::SavedState* state,
                                   const RenderQueueContext* context) const {
  GenerateCommands(cmd_buf, state, context, 0, items_.size());
}

void RenderQueue::GenerateCommands(CommandBuffer* cmd_buf, const CommandBuffer::SavedState* state,
                                   const RenderQueueContext* context, size_t start_index,
                                   size_t count) const {
  size_t end_index = start_index + count;
  FX_DCHECK(end_index <= items_.size());

  while (start_index < end_index) {
    if (state) {
      cmd_buf->RestoreState(*state);
    }

    // The next item to generate commands.
    auto& item = items_[start_index];

    // Obtain the function that will render the item.
    auto func_index = context ? context->render_queue_func_to_use : 0;
    auto render_func = item.render_queue_funcs[func_index];

    // Determine the number of instances to render.
    uint32_t instance_count = 1;
    for (size_t i = start_index + 1; i < end_index && item.object_data == items_[i].object_data &&
                                     render_func == items_[i].render_queue_funcs[func_index];
         ++i) {
      ++instance_count;
    }
    FX_DCHECK(instance_count > 0);  // we guarantee this to clients.
    render_func(cmd_buf, context, &item, instance_count);
    start_index += instance_count;
  }
}

}  // namespace escher
