// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#include "processing_node.h"

#include <lib/ddk/trace/event.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/types.h>

namespace camera {

// Returns a string describing the provided source location.
static std::string Whence(const cpp20::source_location& location) {
  std::string whence = location.file_name() ? location.file_name() : "<unknown>";
  whence += ":" + std::to_string(location.line()) + " (";
  whence += location.function_name() ? location.function_name() : "<unknown>";
  whence += ")";
  return whence;
}

ProcessNode::ProcessNode(async_dispatcher_t* dispatcher, NodeType type,
                         BufferAttachments attachments, FrameCallback frame_callback)
    : dispatcher_(dispatcher),
      type_(type),
      attachments_(attachments),
      frame_callback_(std::move(frame_callback)),
      hwaccel_callbacks_{
          .frame{.frame_ready = &ProcessNode::StaticHwFrameReady, .ctx = this},
          .res_change{.frame_resolution_changed = &ProcessNode::StaticHwFrameResolutionChanged,
                      .ctx = this},
          .remove_task{.task_removed = &ProcessNode::StaticHwTaskRemoved, .ctx = this}} {}

ProcessNode::~ProcessNode() {
  FX_LOGS(INFO) << "~ProcessNode(" << label_ << ")";
  ZX_ASSERT_MSG(shutdown_state_.requested, "Caller destroying node without requesting shutdown.");
  ZX_ASSERT_MSG(shutdown_state_.completed,
                "Caller destroying node without awaiting shutdown completion.");
}

NodeType ProcessNode::Type() const { return type_; }

void ProcessNode::Shutdown(fit::closure callback) {
  TRACE_DURATION("camera", "ProcessNode::Shutdown", "this", this);
  FX_LOGS(INFO) << "ProcessNode::Shutdown(" << label_ << ") - start";
  ZX_ASSERT_MSG(!shutdown_state_.requested, "Caller requested shutdown multiple times.");
  auto nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("camera", "ProcessNode::Shutdown.shutdown", nonce);
  shutdown_state_.requested = true;
  ShutdownImpl([this, nonce, callback = std::move(callback)] {
    TRACE_DURATION("camera", "ProcessNode::Shutdown.callback", "this", this);
    TRACE_FLOW_END("camera", "ProcessNode::Shutdown.shutdown", nonce);
    FX_LOGS(INFO) << "ProcessNode::Shutdown(" << label_ << ") - finish";
    ZX_ASSERT_MSG(shutdown_state_.requested, "Node sent unsolicited shutdown callback.");
    ZX_ASSERT_MSG(!shutdown_state_.completed, "Node sent multiple shutdown callbacks.");
    shutdown_state_.completed = true;
    callback();
  });
}

void ProcessNode::SetLabel(std::string label) {
  FX_LOGS(INFO) << this << " now known as '" << label << "'";
  label_ = std::move(label);
}

void ProcessNode::SendFrame(uint32_t index, frame_metadata_t metadata,
                            fit::closure release_callback) const {
  TRACE_DURATION("camera", "ProcessNode::SendFrame", "this", this, "index", index);
  ZX_ASSERT(metadata.timestamp > 0);
  ZX_ASSERT(metadata.capture_timestamp > 0);
  // If the node is shutting down, immediately release the frame.
  if (shutdown_state_.requested) {
    release_callback();
    return;
  }
  // Wrap the node-provided callback in a trace flow.
  auto nonce = TRACE_NONCE();
  TRACE_FLOW_BEGIN("camera", "ProcessNode::SendFrame.release", nonce);
  FrameToken token(index, [this, index, nonce, callback = std::move(release_callback)] {
    TRACE_DURATION("camera", "ProcessNode::SendFrame.callback", "this", this, "index", index);
    TRACE_FLOW_END("camera", "ProcessNode::SendFrame.release", nonce);
    callback();
  });
  frame_callback_(std::move(token), metadata);
}

const fuchsia::sysmem::BufferCollectionInfo_2& ProcessNode::InputBuffers() const {
  ZX_ASSERT(attachments_.input_collection.has_value());
  return attachments_.input_collection->get().buffers;
}

const std::vector<fuchsia::sysmem::ImageFormat_2>& ProcessNode::InputFormats() const {
  ZX_ASSERT(attachments_.input_formats.has_value());
  return attachments_.input_formats->get();
}

const fuchsia::sysmem::BufferCollectionInfo_2& ProcessNode::OutputBuffers() const {
  ZX_ASSERT(attachments_.output_collection.has_value());
  return attachments_.output_collection->get().buffers;
}

const std::vector<fuchsia::sysmem::ImageFormat_2>& ProcessNode::OutputFormats() const {
  ZX_ASSERT(attachments_.output_formats.has_value());
  return attachments_.output_formats->get();
}

const hw_accel_frame_callback* ProcessNode::GetHwFrameReadyCallback(
    cpp20::source_location location) {
  hwaccel_callbacks_.frame_callsites.push_back(location);
  return &hwaccel_callbacks_.frame;
}

const hw_accel_res_change_callback* ProcessNode::GetHwFrameResolutionChangeCallback(
    cpp20::source_location location) {
  hwaccel_callbacks_.res_change_callsites.push_back(location);
  return &hwaccel_callbacks_.res_change;
}

const hw_accel_remove_task_callback* ProcessNode::GetHwTaskRemovedCallback(
    cpp20::source_location location) {
  hwaccel_callbacks_.remove_task_callsites.push_back(location);
  return &hwaccel_callbacks_.remove_task;
}

void ProcessNode::PostTask(fit::closure task, cpp20::source_location location) {
  auto nonce = TRACE_NONCE();
  auto whence = Whence(location);
  TRACE_DURATION("camera", "PostTask", "whence", whence);
  TRACE_FLOW_BEGIN("camera", "ProcessNodePostTask", nonce);
  async::PostTask(dispatcher_, [nonce, whence, task = std::move(task)] {
    TRACE_DURATION("camera", "PostTask.task", "whence", whence);
    TRACE_FLOW_END("camera", "ProcessNodePostTask", nonce);
    task();
  });
}

void ProcessNode::StaticHwFrameReady(void* ctx, const frame_available_info_t* info) {
  auto node = static_cast<ProcessNode*>(ctx);
  node->PostTask([node, info = *info] { node->HwFrameReady(info); });
}

void ProcessNode::StaticHwFrameResolutionChanged(void* ctx, const frame_available_info_t* info) {
  auto node = static_cast<ProcessNode*>(ctx);
  node->PostTask([node, info = *info] { node->HwFrameResolutionChanged(info); });
}

void ProcessNode::StaticHwTaskRemoved(void* ctx, task_remove_status_t status) {
  auto node = static_cast<ProcessNode*>(ctx);
  node->PostTask([node, status] { node->HwTaskRemoved(status); });
}

}  // namespace camera
