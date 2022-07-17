// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/output_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <safemath/safe_conversions.h>

namespace camera {

constexpr auto kTag = "camera_controller_output_node";

OutputNode::OutputNode(async_dispatcher_t* dispatcher, BufferAttachments attachments)
    : ProcessNode(dispatcher, NodeType::kOutputStream, attachments,
                  [](auto, auto) { FX_NOTREACHED(); }),
      dispatcher_(dispatcher),
      binding_(this) {
  binding_.set_error_handler([this](auto status) { callbacks_.disconnect(); });
}

fpromise::result<std::unique_ptr<OutputNode>, zx_status_t> OutputNode::Create(
    async_dispatcher_t* dispatcher, BufferAttachments attachments,
    fidl::InterfaceRequest<fuchsia::camera2::Stream> request, Callbacks callbacks) {
  ZX_ASSERT(request.is_valid());
  TRACE_DURATION("camera", "OutputNode::Create");
  auto node = std::make_unique<camera::OutputNode>(dispatcher, attachments);
  node->callbacks_ = std::move(callbacks);
  ZX_ASSERT(node->binding_.Bind(std::move(request), node->dispatcher_) == ZX_OK);
  return fpromise::ok(std::move(node));
}

void OutputNode::ProcessFrame(FrameToken token, frame_metadata_t metadata) {
  TRACE_DURATION("camera", "OutputNode::ProcessFrame", "buffer_index", *token);
  if (!started_) {
    return;  // ~token
  }
  if (!binding_.is_bound()) {
    FX_LOGS(WARNING) << this << ": client disconnected? returning frame...";
    return;  // ~token
  }
  TRACE_FLOW_BEGIN("camera", "camera_stream_on_frame_available", metadata.timestamp);
  client_tokens_.emplace(*token, token);
  fuchsia::camera2::FrameAvailableInfo frame_info{};
  frame_info.frame_status = fuchsia::camera2::FrameStatus::OK;
  frame_info.buffer_id = *token;
  frame_info.metadata.set_image_format_index(metadata.image_format_index);
  frame_info.metadata.set_timestamp(safemath::checked_cast<int64_t>(metadata.timestamp));
  frame_info.metadata.set_capture_timestamp(
      safemath::checked_cast<int64_t>(metadata.capture_timestamp));
  ZX_ASSERT(binding_.is_bound());
  binding_.events().OnFrameAvailable(std::move(frame_info));
}

void OutputNode::SetOutputFormat(uint32_t output_format_index, fit::closure callback) {
  TRACE_DURATION("camera", "OutputNode::SetOutputFormat", "format_index", output_format_index);
  callback();
}

void OutputNode::ShutdownImpl(fit::closure callback) {
  TRACE_DURATION("camera", "OutputNode::ShutdownImpl");
  client_tokens_.clear();
  callback();
}

void OutputNode::HwFrameReady(frame_available_info_t info) { FX_NOTREACHED(); }
void OutputNode::HwFrameResolutionChanged(frame_available_info_t info) { FX_NOTREACHED(); }
void OutputNode::HwTaskRemoved(task_remove_status_t status) { FX_NOTREACHED(); }

void OutputNode::Stop() {
  FX_LOGS(INFO) << this << ": Stop()";
  started_ = false;
}

void OutputNode::Start() {
  FX_LOGS(INFO) << this << ": Start()";
  started_ = true;
}

void OutputNode::ReleaseFrame(uint32_t buffer_id) {
  auto element = client_tokens_.extract(buffer_id);
  if (element.empty()) {
    FX_LOGS(INFO) << "Client called ReleaseFrame on non-held buffer " << buffer_id;
  }
}

void OutputNode::AcknowledgeFrameError() {
  FX_LOGST(ERROR, kTag) << __PRETTY_FUNCTION__ << " not implemented";
}

void OutputNode::SetRegionOfInterest(float x_min, float y_min, float x_max, float y_max,
                                     SetRegionOfInterestCallback callback) {
  TRACE_DURATION("camera", "OutputNode::SetRegionOfInterest");
  callbacks_.set_region_of_interest(x_min, y_min, x_max, y_max, std::move(callback));
}

void OutputNode::SetImageFormat(uint32_t image_format_index, SetImageFormatCallback callback) {
  TRACE_DURATION("camera", "OutputNode::SetImageFormat");
  callbacks_.set_image_format(image_format_index, std::move(callback));
}

void OutputNode::GetImageFormats(GetImageFormatsCallback callback) {
  TRACE_DURATION("camera", "OutputNode::GetImageFormats");
  callbacks_.get_image_formats(std::move(callback));
}

void OutputNode::GetBuffers(GetBuffersCallback callback) {
  TRACE_DURATION("camera", "OutputNode::GetBuffers");
  callbacks_.get_buffers(std::move(callback));
}

}  // namespace camera
