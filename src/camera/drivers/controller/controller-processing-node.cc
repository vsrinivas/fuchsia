// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "controller-processing-node.h"

#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <src/lib/fxl/logging.h>

#include "stream_protocol.h"

namespace camera {

void CameraProcessNode::OnReadyToProcess(uint32_t buffer_index) {
  ZX_ASSERT_MSG(false, "Unknown NodeType\n");
}

void CameraProcessNode::OnFrameAvailable(uint32_t buffer_index) {
  client_stream_->FrameReady(buffer_index);
}

void CameraProcessNode::OnReleaseFrame(uint32_t buffer_index) {
  if (type_ == NodeType::kInputStream) {
    isp_stream_protocol_->ReleaseFrame(buffer_index);
    return;
  }
  ZX_ASSERT_MSG(false, "Unknown NodeType\n");
}

void CameraProcessNode::OnStartStreaming() {
  enabled_ = true;
  if (type_ == NodeType::kInputStream) {
    isp_stream_protocol_->Start();
    return;
  }
  ZX_ASSERT_MSG(false, "Unknown NodeType\n");
}

void CameraProcessNode::OnStopStreaming() {
  if (type_ == NodeType::kInputStream) {
    isp_stream_protocol_->Stop();
    return;
  }
  ZX_ASSERT_MSG(false, "Unknown NodeType\n");
}

}  // namespace camera
