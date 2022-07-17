// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/input_node.h"

#include <fuchsia/hardware/isp/c/banjo.h>
#include <lib/ddk/trace/event.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"
#include "src/devices/lib/sysmem/sysmem.h"

namespace camera {

constexpr auto kTag = "camera_controller_input_node";

InputNode::InputNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
                     FrameCallback frame_callback, const ddk::IspProtocolClient& isp)
    : ProcessNode(dispatcher, NodeType::kInputStream, attachments, std::move(frame_callback)) {}

fpromise::result<std::unique_ptr<InputNode>, zx_status_t> InputNode::Create(
    async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
    const ddk::IspProtocolClient& isp, const StreamCreationData& info) {
  // TODO(100525): this makes very specific assumptions about the layout of the monitoring config
  const auto& inode = info.stream_type() == fuchsia::camera2::CameraStreamType::MONITORING
                          ? info.roots[1]
                          : info.roots[0];
  // Create Input Node
  auto pnode =
      std::make_unique<camera::InputNode>(dispatcher, attachments, std::move(frame_callback), isp);

  uint8_t isp_stream_type = 0;
  if (inode.input_stream_type == fuchsia::camera2::CameraStreamType::FULL_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_FULL_RESOLUTION;
  } else if (inode.input_stream_type == fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_DOWNSCALED;
  } else {
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }

  // Use a BufferCollectionHelper to manage the conversion
  // between buffer collection representations.
  BufferCollectionHelper buffer_collection_helper(pnode->OutputBuffers());

  auto image_format = ConvertHlcppImageFormat2toCType(inode.image_formats[0]);

  buffer_collection_info_2 temp_buffer_collection;
  image_format_2_t temp_image_format;
  sysmem::buffer_collection_info_2_banjo_from_fidl(*buffer_collection_helper.GetC(),
                                                   temp_buffer_collection);
  sysmem::image_format_2_banjo_from_fidl(image_format, temp_image_format);
  output_stream_protocol_t isp_stream_protocol{};
  auto status = isp.CreateOutputStream(
      &temp_buffer_collection, &temp_image_format,
      reinterpret_cast<const frame_rate_t*>(&inode.output_frame_rate), isp_stream_type,
      pnode->GetHwFrameReadyCallback(), &isp_stream_protocol);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to create output stream on ISP";
    return fpromise::error(status);
  }
  pnode->stream_ = ddk::OutputStreamProtocolClient(&isp_stream_protocol);

  // Note that as clients can only access the camera via camera3, and camera3 only exposes a
  // device-wide mute state (corresponding to StartStreaming/StopStreaming), individual camera2
  // stream start/stop requests from the client are ignored.
  ZX_ASSERT(pnode->stream_.Start() == ZX_OK);

  return fpromise::ok(std::move(pnode));
}

void InputNode::ProcessFrame(FrameToken token, frame_metadata_t metadata) {
  ZX_PANIC("InputNode cannot process frames!");
}

void InputNode::SetOutputFormat(uint32_t output_format_index, fit::closure callback) {
  ZX_PANIC("InputNode cannot set output format!");
}

void InputNode::ShutdownImpl(fit::closure callback) {
  TRACE_DURATION("camera", "InputNode::ShutdownImpl");
  ZX_ASSERT(!shutdown_callback_);
  shutdown_callback_ = std::move(callback);

  isp_stream_shutdown_callback_t isp_stream_shutdown_cb{
      .shutdown_complete =
          [](void* ctx, zx_status_t status) {
            ZX_ASSERT(status == ZX_OK);
            auto node = static_cast<decltype(this)>(ctx);
            node->PostTask([node] { node->shutdown_callback_(); });
          },
      .ctx = this,
  };
  StopStreaming();
  stream_.Shutdown(&isp_stream_shutdown_cb);
}

void InputNode::HwFrameReady(frame_available_info_t info) {
  TRACE_DURATION("camera", "InputNode::HwFrameReady", "status",
                 static_cast<uint32_t>(info.frame_status), "buffer_index", info.buffer_id);
  // Don't do anything further with error frames.
  if (info.frame_status != FRAME_STATUS_OK) {
    constexpr auto kErrorFrameMinLogInterval = zx::sec(1);
    auto now = zx::clock::get_monotonic();
    if (now >= last_frame_error_logged_ + kErrorFrameMinLogInterval) {
      FX_LOGST(ERROR, kTag) << "failed input frame: " << static_cast<uint32_t>(info.frame_status);
      TRACE_INSTANT("camera", "bad_status", TRACE_SCOPE_THREAD, "frame_status",
                    static_cast<uint32_t>(info.frame_status));
      last_frame_error_logged_ = now;
    }
    return;
  }
  if (info.metadata.timestamp == 0) {
    FX_LOGST(ERROR, kTag) << "missing timestamp on input buffer " << info.buffer_id;
    stream_.ReleaseFrame(info.buffer_id);
    return;
  }

  // Use the timestamp of the input as the capture time.
  auto modified = info;
  modified.metadata.capture_timestamp = info.metadata.timestamp;

  // Send the frame onward.
  SendFrame(modified.buffer_id, modified.metadata,
            [this, buffer_index = modified.buffer_id] { stream_.ReleaseFrame(buffer_index); });
}

void InputNode::HwFrameResolutionChanged(frame_available_info_t info) {}

void InputNode::HwTaskRemoved(task_remove_status_t status) {}

void InputNode::StartStreaming() {
  TRACE_DURATION("camera", "InputNode::StartStreaming");
  stream_.Start();
}

void InputNode::StopStreaming() {
  TRACE_DURATION("camera", "InputNode::StopStreaming");
  stream_.Stop();
}

}  // namespace camera
