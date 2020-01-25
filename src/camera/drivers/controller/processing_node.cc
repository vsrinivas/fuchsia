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

void ProcessNode::UpdateFrameCounterForAllChildren() {
  // Update the current frame counter.
  for (auto& node : child_nodes_) {
    node->AddToCurrentFrameCount(node->output_fps());
  }
}

bool ProcessNode::NeedToDropFrame() {
  return !enabled_ || AllChildNodesDisabled() ||
         std::none_of(child_nodes_.begin(), child_nodes_.end(),
                      [this](auto& node) { return node->current_frame_count() >= output_fps(); });
}

void ProcessNode::OnFrameAvailable(const frame_available_info_t* info) {
  ZX_ASSERT_MSG(type_ != NodeType::kOutputStream, "Invalid for OuputNode");
  frame_available_info_t local_info = *info;
  async::PostTask(dispatcher_, [this, local_info]() {
    // Free up parent's frame.
    if (type_ != kInputStream && !shutdown_requested_) {
      parent_node_->OnReleaseFrame(local_info.metadata.input_buffer_index);
    }
    if (enabled_ && local_info.frame_status == FRAME_STATUS_OK) {
      for (auto& node : child_nodes_) {
        if (node->enabled()) {
          // Check if this frame needs to be passed on to the next node.
          if (node->current_frame_count() >= output_fps()) {
            node->SubtractFromCurrentFrameCount(output_fps());
            {
              fbl::AutoLock al(&in_use_buffer_lock_);
              ZX_ASSERT(local_info.buffer_id < in_use_buffer_count_.size());
              in_use_buffer_count_[local_info.buffer_id]++;
            }
            node->OnReadyToProcess(&local_info);
          }
        }
      }
      return;
    }
    // TODO(braval): Handle all frame_status errors.);
  });
}

void ProcessNode::OnStartStreaming() {
  if (!shutdown_requested_ && !enabled_) {
    enabled_ = true;
    parent_node_->OnStartStreaming();
  }
}

bool ProcessNode::AllChildNodesDisabled() {
  return std::none_of(child_nodes_.begin(), child_nodes_.end(),
                      [](auto& node) { return node->enabled(); });
}

void ProcessNode::OnStopStreaming() {
  if (!shutdown_requested_ && enabled_) {
    if (AllChildNodesDisabled()) {
      enabled_ = false;
      parent_node_->OnStopStreaming();
    }
  }
}

}  // namespace camera
