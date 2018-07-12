// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/renderer/render_queue.h"

namespace escher {

RenderQueue::RenderQueue() = default;

void RenderQueue::Sort() {
  std::stable_sort(items_.begin(), items_.end(),
                   [](const RenderQueueItem& a, const RenderQueueItem& b) {
                     return a.sort_key < b.sort_key;
                   });
}

void RenderQueue::GenerateCommands(
    CommandBuffer* cmd_buf, const CommandBuffer::SavedState* state) const {
  GenerateCommands(cmd_buf, state, 0, items_.size());
}

void RenderQueue::GenerateCommands(CommandBuffer* cmd_buf,
                                   const CommandBuffer::SavedState* state,
                                   size_t start_index, size_t count) const {
  size_t end_index = start_index + count;
  FXL_DCHECK(end_index <= items_.size());

  while (start_index < end_index) {
    if (state) {
      cmd_buf->RestoreState(*state);
    }

    auto& item = items_[start_index];
    size_t instance_count = 1;
    for (size_t i = start_index + 1;
         i < end_index && item.object_data == items_[i].object_data &&
         item.render == items_[i].render;
         ++i) {
      ++instance_count;
    }
    FXL_DCHECK(instance_count > 0);  // we guarantee this to clients.
    item.render(cmd_buf, &item, instance_count);
    start_index += instance_count;
  }
}

}  // namespace escher
