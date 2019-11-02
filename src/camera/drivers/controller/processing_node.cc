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
  // TODO(braval): Add support for other types of nodes
}

void ProcessNode::OnFrameAvailable(uint32_t buffer_index) {
  // This API is not in use for |kOutputStream|
  ZX_ASSERT(type_ != NodeType::kOutputStream);

  // TODO(braval): Free up the frame for the parent
  // We will be modifying the signature of this API which would also have
  // information of which frame to free for the producer (parent)

  // Loop through all the child nodes and call their |OnFrameAvailable|
  for (auto& i : child_nodes_info_) {
    auto& child_node = i.child_node;
    // TODO(braval): Regulate frame rate here
    if (child_node->enabled()) {
      child_node->OnReadyToProcess(buffer_index);
    }
  }
}

void ProcessNode::OnReleaseFrame(uint32_t buffer_index) {
  // First release this nodes Frames (GDC, GE2D)
  switch (type_) {
    case NodeType::kGdc: {
      // TODO(braval): Inform the HW accelerator for freeing up the frames
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
      // Need to just call parent's Release Frame which is done below
      break;
    }
    default: {
      ZX_ASSERT_MSG(false, "Unknown NodeType\n");
      return;
    }
  }
  // Call parents ReleaseFrame()
  // TODO(braval): Handle the case where we need to ensure that all
  // children have freed up the buffers.
  parent_node_->OnReleaseFrame(buffer_index);
}

void ProcessNode::OnStartStreaming() {
  enabled_ = true;
  if (type_ == NodeType::kInputStream) {
    isp_stream_protocol_->Start();
    return;
  }
  parent_node_->OnStartStreaming();
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
