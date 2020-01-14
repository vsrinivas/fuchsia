// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "processing_node.h"

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <src/lib/fxl/logging.h>

#include "stream_protocol.h"

namespace camera {

void ProcessNode::OnFrameAvailable(const frame_available_info_t* info) {
  ZX_ASSERT_MSG(type_ != NodeType::kOutputStream, "Invalid for OuputNode");
  fbl::AutoLock guard(&event_queue_lock_);
  frame_available_info_t local_info = *info;
  event_queue_.emplace([this, local_info]() {
    // Free up parent's frame
    if (type_ != kInputStream && !shutdown_requested_) {
      parent_node_->OnReleaseFrame(local_info.metadata.input_buffer_index);
    }

    if (enabled_ && local_info.frame_status == FRAME_STATUS_OK) {
      for (auto& child_node : child_nodes_) {
        // TODO(braval): Regulate frame rate here
        if (child_node->enabled()) {
          {
            fbl::AutoLock al(&in_use_buffer_lock_);
            ZX_ASSERT(local_info.buffer_id < in_use_buffer_count_.size());
            in_use_buffer_count_[local_info.buffer_id]++;
          }
          child_node->OnReadyToProcess(local_info.buffer_id);
        }
      }
      return;
    }
    // TODO(braval): Handle all frame_status errors.

    fbl::AutoLock guard(&event_queue_lock_);
    event_queue_.pop();
  });
  event_queue_.back().Post(dispatcher_);
}

void ProcessNode::OnStartStreaming() {
  if (!enabled_) {
    enabled_ = true;
    parent_node_->OnStartStreaming();
  }
}

bool ProcessNode::AllChildNodesDisabled() {
  for (auto& child_node : child_nodes_) {
    if (child_node->enabled()) {
      return false;
    }
  }
  return true;
}

void ProcessNode::OnStopStreaming() {
  if (enabled_) {
    if (AllChildNodesDisabled()) {
      enabled_ = false;
      parent_node_->OnStopStreaming();
    }
  }
}

}  // namespace camera
