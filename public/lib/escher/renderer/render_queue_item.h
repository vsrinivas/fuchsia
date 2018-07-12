// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_RENDERER_RENDER_QUEUE_ITEM_H_
#define LIB_ESCHER_RENDERER_RENDER_QUEUE_ITEM_H_

#include "lib/escher/vk/command_buffer.h"

namespace escher {

// RenderQueueItem is a sortable item stored in a RenderQueue.  It contains
// pointers to object/instance data as well as a RenderFunc: a function that
// knows how to interpret the object/instance data in order to emit commands
// into a Vulkan command buffer.
struct RenderQueueItem {
  uint64_t sort_key;

  const void* object_data;
  const void* instance_data;

  // Render callback that knows how to interpret the |object_data| and
  // |instance_data| fields.  The number of instances to be rendered is given by
  // |instance_count|, which is guaranteed to be >= 1.  If there are multiple
  // instances:
  //   - the instance-specific data for the i-th instance is given by:
  //     "items[i].instance_data".
  //   - each of the items "items[0]" to "items[instance_count - 1]" are
  //     guaranteed to have the same |object_data| and |render| function.
  typedef void (*RenderFunc)(CommandBuffer* cmd_buf,
                             const RenderQueueItem* items,
                             uint32_t instance_count);
  RenderFunc render;
};

}  // namespace escher

#endif  // LIB_ESCHER_RENDERER_RENDER_QUEUE_ITEM_H_
