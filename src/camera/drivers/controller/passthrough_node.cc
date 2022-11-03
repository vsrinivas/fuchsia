// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/passthrough_node.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>

#include <safemath/safe_conversions.h>

namespace camera {

PassthroughNode::PassthroughNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
                                 FrameCallback frame_callback,
                                 const camera::InternalConfigNode& internal_passthrough_node)
    : ProcessNode(dispatcher, NodeType::kPassthrough, attachments, std::move(frame_callback)) {}

fpromise::result<std::unique_ptr<PassthroughNode>, zx_status_t> PassthroughNode::Create(
    async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
    const InternalConfigNode& internal_passthrough_node, const StreamCreationData& info) {
  TRACE_DURATION("camera", "PassthroughNode::Create");
  if (internal_passthrough_node.output_constraints) {
    FX_LOGS(INFO) << "Passthrough node does not support copying.";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  auto node = std::make_unique<camera::PassthroughNode>(
      dispatcher, attachments, std::move(frame_callback), internal_passthrough_node);
  return fpromise::ok(std::move(node));
}

void PassthroughNode::ProcessFrame(FrameToken token, frame_metadata_t metadata) {
  TRACE_DURATION("camera", "PassthroughNode::ProcessFrame", "buffer_index", *token);
  SendFrame(*token, metadata, [token] {
    // ~token
  });
}

void PassthroughNode::SetOutputFormat(uint32_t output_format_index, fit::closure callback) {
  TRACE_DURATION("camera", "PassthroughNode::SetOutputFormat", "format_index", output_format_index);
  // no-op
}

void PassthroughNode::ShutdownImpl(fit::closure callback) {
  TRACE_DURATION("camera", "PassthroughNode::ShutdownImpl");
  callback();
}

void PassthroughNode::HwFrameReady(frame_available_info_t info) { FX_NOTREACHED(); }
void PassthroughNode::HwFrameResolutionChanged(frame_available_info_t info) { FX_NOTREACHED(); }
void PassthroughNode::HwTaskRemoved(task_remove_status_t status) { FX_NOTREACHED(); }

}  // namespace camera
