// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_RENDER_QUEUE_H_
#define LIB_ESCHER_RENDERER_RENDER_QUEUE_H_

#include "lib/escher/renderer/render_queue_item.h"
#include "lib/escher/vk/command_buffer.h"

namespace escher {

// RenderQueue provides an abstraction to allow renderable objects of different
// types to be sorted according to a uniform criterion before emitting commands
// into a Vulkan command buffer.  Client use of RenderQueue typically follows
// these steps:
// - call Push() repeatedly to add all renderable objects to the queue.
// - call Sort() once to sort the items according to their integer sort key.
// - call GenerateCommands() once to add Vulkan commands to a command buffer.
// - call clear() to clear all items, preparing the queue to be used next frame.
//
// See the method documentation for additional details.
class RenderQueue final {
 public:
  RenderQueue();
  RenderQueue(const RenderQueue& other) = delete;

  // The method arguments are encapsulated in a RenderQueueItem.  Since
  // |object_data| and |instance_data| are void*, it is clearly up to the caller
  // to manage their lifecycle (including any data that they point to).  For
  // example, any CPU memory must be valid until Clear() is called, and any GPU
  // resources must be kept alive until all Vulkan command buffers that
  // reference them are finished.
  void Push(uint64_t sort_key, const void* object_data,
            const void* instance_data, RenderQueueItem::RenderFunc render_func);

  // Performs a stable sort of all items, according to the |sort_key| provided
  // to Push().  Using a simple integer as the sort key allows clients a large
  // degree of flexibility in defining sort criteria.  For example, translucent
  // objects must be sorted back-to-front after all opaque objects, whereas
  // opaque objects are more efficiently rendered front-to-back.
  void Sort();

  // Generate Vulkan commands for items in the queue, by iterating over the
  // items and invoking each item's RenderFunc on it.  If multiple consecutive
  // items have the same |object_data|, they are batched into a single call to
  // RenderFunc; in this case the number of consecutive items is encoded in the
  // |instance_count|.  Otherwise, |instance_count| == 1.
  //
  // This is typically called after Sort(), but this is not necessary.  For
  // example, clients may choose to not call Sort() in order to profile the
  // performance difference of sorting vs. not sorting.
  void GenerateCommands(CommandBuffer* cmd_buf,
                        const CommandBuffer::SavedState* state) const;

  // This variant of GenerateCommands() behaves similarly to the one above,
  // except that it only generates commands for a sub-range of the items in the
  // queue.  This is typically used for debugging.
  void GenerateCommands(CommandBuffer* cmd_buf,
                        const CommandBuffer::SavedState* state,
                        size_t start_index, size_t count) const;

  void clear() { items_.clear(); }
  size_t size() const { return items_.size(); }

 protected:
  std::vector<RenderQueueItem> items_;
};

// Inline function definitions.

inline void RenderQueue::Push(uint64_t sort_key, const void* object_data,
                              const void* instance_data,
                              RenderQueueItem::RenderFunc render_func) {
  items_.push_back({sort_key, object_data, instance_data, render_func});
}

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_RENDER_QUEUE_H_
