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
  // Free up parent's frame
  if (type_ != kInputStream) {
    parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
  }

  if (info->frame_status == FRAME_STATUS_OK) {
    for (auto& i : child_nodes_info_) {
      auto& child_node = i.child_node;
      // TODO(braval): Regulate frame rate here
      if (child_node->enabled()) {
        {
          fbl::AutoLock al(&in_use_buffer_lock_);
          ZX_ASSERT(info->buffer_id < in_use_buffer_count_.size());
          in_use_buffer_count_[info->buffer_id]++;
        }
        child_node->OnReadyToProcess(info->buffer_id);
      }
    }
    return;
  }
  // TODO(braval): Handle all frame_status errors.
}

void ProcessNode::OnReleaseFrame(uint32_t buffer_index) {
  fbl::AutoLock al(&in_use_buffer_lock_);
  ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
  in_use_buffer_count_[buffer_index]--;
  if (in_use_buffer_count_[buffer_index] != 0) {
    return;
  }

  // First release this nodes Frames (GDC, GE2D)
  switch (type_) {
    case NodeType::kGe2d: {
      // TODO(braval): Inform the HW accelerator for freeing up the frames
      break;
    }
    case NodeType::kInputStream: {
      isp_stream_protocol_->ReleaseFrame(buffer_index);
      return;
    }
    default: {
      ZX_ASSERT_MSG(false, "Unknown NodeType\n");
      return;
    }
  }
}

void ProcessNode::OnStartStreaming() {
  if (!enabled_) {
    enabled_ = true;
    if (type_ == NodeType::kInputStream) {
      isp_stream_protocol_->Start();
      return;
    }
    parent_node_->OnStartStreaming();
  }
}

bool ProcessNode::AllChildNodesDisabled() {
  for (auto& i : child_nodes_info_) {
    auto& child_node = i.child_node;
    if (child_node->enabled()) {
      return false;
    }
  }
  return true;
}

void ProcessNode::OnStopStreaming() {
  if (AllChildNodesDisabled()) {
    enabled_ = false;
    if (type_ == NodeType::kInputStream) {
      isp_stream_protocol_->Stop();
    } else {
      parent_node_->OnStopStreaming();
    }
  }
}

void ProcessNode::OnShutdown() {
  switch (type_) {
    case NodeType::kGe2d: {
      // TODO(braval): Add support for this.
      break;
    }
    case NodeType::kInputStream: {
      // TODO(braval): Add support for this.
      break;
    }
    default: {
      ZX_ASSERT_MSG(false, "Unknown NodeType\n");
      return;
    }
  }
}

}  // namespace camera
