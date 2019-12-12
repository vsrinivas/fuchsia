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

void ProcessNode::OnReadyToProcess(uint32_t buffer_index) {
  // If it's the output stream node, we have to notify the client about
  // the frame being available
  if (type_ == NodeType::kOutputStream) {
    ZX_ASSERT(client_stream_ != nullptr);
    client_stream_->FrameReady(buffer_index);
    return;
  }

  if (type_ == NodeType::kGdc) {
    ZX_ASSERT(ZX_OK == gdc_.ProcessFrame(hw_accelerator_task_index_, buffer_index));
  }
  // TODO(braval): Add support for other types of nodes
}

void ProcessNode::OnFrameAvailable(uint32_t buffer_index) {
  // This API is only used for |kInputStream| because ISP uses
  // a different callback compared to GDC and GE2D.
  ZX_ASSERT(type_ == NodeType::kInputStream);

  // Loop through all the child nodes and call their |OnFrameAvailable|
  for (auto& i : child_nodes_info_) {
    auto& child_node = i.child_node;
    // TODO(braval): Regulate frame rate here
    if (child_node->enabled()) {
      {
        fbl::AutoLock al(&in_use_buffer_lock_);
        ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
        in_use_buffer_count_[buffer_index]++;
      }
      child_node->OnReadyToProcess(buffer_index);
    }
  }
}

void ProcessNode::OnFrameAvailable(const frame_available_info_t* info) {
  // This API is not in use for |kOutputStream|
  ZX_ASSERT(type_ != NodeType::kOutputStream);

  // Free up parent's frame
  parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
  if (info->frame_status == FRAME_STATUS_OK) {
    // Loop through all the child nodes and call their |OnFrameAvailable|
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
  if (type_ != NodeType::kOutputStream) {
    fbl::AutoLock al(&in_use_buffer_lock_);
    ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
    in_use_buffer_count_[buffer_index]--;
    if (in_use_buffer_count_[buffer_index] != 0) {
      return;
    }
  }

  // First release this nodes Frames (GDC, GE2D)
  switch (type_) {
    case NodeType::kGdc: {
      gdc_.ReleaseFrame(hw_accelerator_task_index_, buffer_index);
      break;
    }
    case NodeType::kGe2d: {
      // TODO(braval): Inform the HW accelerator for freeing up the frames
      break;
    }
    case NodeType::kInputStream: {
      isp_stream_protocol_->ReleaseFrame(buffer_index);
      return;
    }
    case NodeType::kOutputStream: {
      parent_node_->OnReleaseFrame(buffer_index);
      break;
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

}  // namespace camera
