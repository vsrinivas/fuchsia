// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/ge2d_node.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>

#include <safemath/safe_conversions.h>

#include "src/camera/lib/format_conversion/format_conversion.h"
#include "src/devices/lib/sysmem/sysmem.h"

namespace camera {

constexpr auto kTag = "camera_controller_ge2d_node";

Ge2dNode::Ge2dNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
                   FrameCallback frame_callback, const ddk::Ge2dProtocolClient& ge2d,
                   const camera::InternalConfigNode& internal_ge2d_node)
    : ProcessNode(dispatcher, NodeType::kGe2d, attachments, std::move(frame_callback)),
      ge2d_(ge2d),
      task_type_(internal_ge2d_node.ge2d_info.config_type),
      in_place_(!internal_ge2d_node.output_constraints),
      current_transform_(internal_ge2d_node.ge2d_info.resize) {}

fpromise::result<std::unique_ptr<Ge2dNode>, zx_status_t> Ge2dNode::Create(
    async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
    const LoadFirmwareCallback& load_firmware, const ddk::Ge2dProtocolClient& ge2d,
    const InternalConfigNode& internal_ge2d_node, const StreamCreationData& info) {
  TRACE_DURATION("camera", "Ge2dNode::Create");
  auto node = std::make_unique<camera::Ge2dNode>(dispatcher, attachments, std::move(frame_callback),
                                                 ge2d, internal_ge2d_node);

  fuchsia_sysmem::wire::BufferCollectionInfo2 input_buffer_collection =
      ConvertToWireTypeBufferCollectionInfo2(node->InputBuffers());
  fuchsia_sysmem::wire::BufferCollectionInfo2 output_buffer_collection =
      ConvertToWireTypeBufferCollectionInfo2(node->in_place_ ? node->InputBuffers()
                                                             : node->OutputBuffers());

  std::vector<image_format_2_t> output_image_formats_c;
  for (auto& format : internal_ge2d_node.image_formats) {
    image_format_2_t value;
    auto original = GetImageFormatFromBufferCollection(output_buffer_collection, format.coded_width,
                                                       format.coded_height);
    sysmem::image_format_2_banjo_from_fidl(original, value);
    output_image_formats_c.push_back(value);
  }

  std::vector<image_format_2_t> input_image_formats_c;
  for (auto& format : node->InputFormats()) {
    image_format_2_t value;
    auto original = GetImageFormatFromBufferCollection(input_buffer_collection, format.coded_width,
                                                       format.coded_height);
    sysmem::image_format_2_banjo_from_fidl(original, value);
    input_image_formats_c.push_back(value);
  }

  // Initialize the GE2D to get a unique task index.
  buffer_collection_info_2 temp_input_collection, temp_output_collection;
  sysmem::buffer_collection_info_2_banjo_from_fidl(std::move(input_buffer_collection),
                                                   temp_input_collection);
  sysmem::buffer_collection_info_2_banjo_from_fidl(std::move(output_buffer_collection),
                                                   temp_output_collection);
  switch (internal_ge2d_node.ge2d_info.config_type) {
    case Ge2DConfig::GE2D_RESIZE: {
      zx_status_t status = ge2d.InitTaskResize(
          &temp_input_collection, &temp_output_collection, &node->current_transform_,
          input_image_formats_c.data(), output_image_formats_c.data(),
          output_image_formats_c.size(), info.image_format_index, node->GetHwFrameReadyCallback(),
          node->GetHwFrameResolutionChangeCallback(), node->GetHwTaskRemovedCallback(),
          &node->task_index_);
      if (status != ZX_OK) {
        FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GE2D resize task";
        return fpromise::error(status);
      }
      break;
    }
    case Ge2DConfig::GE2D_WATERMARK: {
      std::vector<zx::vmo> watermark_vmos;
      for (auto watermark : internal_ge2d_node.ge2d_info.watermark) {
        auto result = load_firmware(watermark.filename);
        if (result.is_error()) {
          FX_PLOGST(ERROR, kTag, result.error()) << "Failed to load the watermark image";
          return fpromise::error(result.error());
        }
        auto [vmo, size] = result.take_value();
        watermark_vmos.push_back(std::move(vmo));
      }

      std::vector<water_mark_info> watermarks_info;
      for (uint32_t i = 0; i < internal_ge2d_node.ge2d_info.watermark.size(); i++) {
        water_mark_info info;
        info.loc_x = internal_ge2d_node.ge2d_info.watermark[i].loc_x;
        info.loc_y = internal_ge2d_node.ge2d_info.watermark[i].loc_y;
        auto format = ConvertHlcppImageFormat2toWireType(
            internal_ge2d_node.ge2d_info.watermark[i].image_format);
        sysmem::image_format_2_banjo_from_fidl(format, info.wm_image_format);
        info.watermark_vmo = watermark_vmos[i].release();
        constexpr float kGlobalAlpha = 200.f / 255;
        info.global_alpha = kGlobalAlpha;
        watermarks_info.push_back(info);
      }

      auto cleanup = fit::defer([watermarks_info]() {
        for (auto info : watermarks_info) {
          ZX_ASSERT_MSG(ZX_OK == zx_handle_close(info.watermark_vmo),
                        "Failed to free up watermark VMOs");
        }
      });

      zx_status_t status = ZX_OK;
      if (node->in_place_) {
        status = ge2d.InitTaskInPlaceWaterMark(
            &temp_input_collection, watermarks_info.data(), watermarks_info.size(),
            input_image_formats_c.data(), input_image_formats_c.size(), info.image_format_index,
            node->GetHwFrameReadyCallback(), node->GetHwFrameResolutionChangeCallback(),
            node->GetHwTaskRemovedCallback(), &node->task_index_);
      } else {
        status = ge2d.InitTaskWaterMark(&temp_input_collection, &temp_output_collection,
                                        watermarks_info.data(), watermarks_info.size(),
                                        input_image_formats_c.data(), input_image_formats_c.size(),
                                        info.image_format_index, node->GetHwFrameReadyCallback(),
                                        node->GetHwFrameResolutionChangeCallback(),
                                        node->GetHwTaskRemovedCallback(), &node->task_index_);
      }
      if (status != ZX_OK) {
        FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GE2D watermark task";
        return fpromise::error(status);
      }
      break;
    }
    default: {
      FX_LOGST(ERROR, kTag) << "Unkwon config type";
      return fpromise::error(ZX_ERR_INVALID_ARGS);
    }
  }

  return fpromise::ok(std::move(node));
}

void Ge2dNode::ProcessFrame(FrameToken token, frame_metadata_t metadata) {
  TRACE_DURATION("camera", "Ge2dNode::ProcessFrame", "buffer_index", *token);
  if (shutdown_callback_) {
    // ~token
    return;
  }
  input_frame_queue_.push(token);
  ZX_ASSERT(ge2d_.ProcessFrame(task_index_, *token, metadata.capture_timestamp) == ZX_OK);
}

void Ge2dNode::SetOutputFormat(uint32_t output_format_index, fit::closure callback) {
  TRACE_DURATION("camera", "Ge2dNode::SetOutputFormat", "format_index", output_format_index);
  if (task_type_ == Ge2DConfig::GE2D_WATERMARK) {
    ge2d_.SetInputAndOutputResolution(task_index_, output_format_index);
  } else {
    ge2d_.SetOutputResolution(task_index_, output_format_index);
  }
  format_callback_ = std::move(callback);
}

void Ge2dNode::ShutdownImpl(fit::closure callback) {
  TRACE_DURATION("camera", "Ge2dNode::ShutdownImpl");
  ZX_ASSERT(!shutdown_callback_);
  shutdown_callback_ = std::move(callback);

  // Request GE2D to shutdown.
  ge2d_.RemoveTask(task_index_);
}

void Ge2dNode::HwFrameReady(frame_available_info_t info) {
  TRACE_DURATION("camera", "Ge2dNode::HwFrameReady", "status", info.frame_status, "buffer_index",
                 info.buffer_id);
  auto input_token = std::move(input_frame_queue_.front());
  input_frame_queue_.pop();

  // Don't do anything further with error frames.
  if (info.frame_status != FRAME_STATUS_OK) {
    FX_LOGST(ERROR, kTag) << "failed ge2d frame: " << static_cast<uint32_t>(info.frame_status);
    return;
  }

  // Send the frame onward. If this is an "in-place" operation, defer releasing the input buffer
  // until the "output" buffer is released.
  std::optional<FrameToken> maybe_input_token;
  if (in_place_) {
    maybe_input_token = input_token;
  }
  SendFrame(info.buffer_id, info.metadata,
            [this, buffer_index = info.buffer_id, maybe_input_token] {
              ge2d_.ReleaseFrame(task_index_, buffer_index);
              // ~maybe_input_token
            });
}

void Ge2dNode::HwFrameResolutionChanged(frame_available_info_t info) {
  TRACE_DURATION("camera", "Ge2dNode::HwFrameResolutionChanged");
  format_callback_();
  format_callback_ = nullptr;
}

void Ge2dNode::HwTaskRemoved(task_remove_status_t status) {
  TRACE_DURATION("camera", "Ge2dNode::HwTaskRemoved");
  ZX_ASSERT(status == TASK_REMOVE_STATUS_OK);
  ZX_ASSERT(shutdown_callback_);
  if (!input_frame_queue_.empty()) {
    FX_LOGS(WARNING)
        << "GE2D driver completed task removal but did not complete processing for all "
           "frames it was sent. These will be manually released.";
    while (!input_frame_queue_.empty()) {
      input_frame_queue_.pop();
    }
  }
  shutdown_callback_();
}

zx_status_t Ge2dNode::SetCropRect(float x_min, float y_min, float x_max, float y_max) {
  TRACE_DURATION("camera", "Ge2dNode::SetCropRect");
  if (task_type_ != Ge2DConfig::GE2D_RESIZE) {
    return ZX_ERR_INVALID_ARGS;
  }

  if (x_max < x_min) {
    FX_LOGST(DEBUG, kTag) << "Invalid crop parameters: x_max(" << x_min << ") < x_min(" << x_min
                          << ")";
    return ZX_ERR_INVALID_ARGS;
  }

  if (y_max < y_min) {
    FX_LOGST(DEBUG, kTag) << "Invalid crop parameters: y_max(" << y_min << ") < y_min(" << y_min
                          << ")";
    return ZX_ERR_INVALID_ARGS;
  }

  x_min = std::clamp(x_min, 0.0f, 1.0f);
  x_max = std::clamp(x_max, 0.0f, 1.0f);
  y_min = std::clamp(y_min, 0.0f, 1.0f);
  y_max = std::clamp(y_max, 0.0f, 1.0f);

  auto& input_image_format = InputFormats().at(0);
  auto normalized_x_min = safemath::checked_cast<uint32_t>(
      x_min * safemath::checked_cast<float>(input_image_format.coded_width) + 0.5f);
  auto normalized_y_min = safemath::checked_cast<uint32_t>(
      y_min * safemath::checked_cast<float>(input_image_format.coded_height) + 0.5f);
  auto normalized_x_max = safemath::checked_cast<uint32_t>(
      x_max * safemath::checked_cast<float>(input_image_format.coded_width) + 0.5f);
  auto normalized_y_max = safemath::checked_cast<uint32_t>(
      y_max * safemath::checked_cast<float>(input_image_format.coded_height) + 0.5f);

  auto width = normalized_x_max - normalized_x_min;
  auto height = normalized_y_max - normalized_y_min;

  rect_t crop = {
      .x = normalized_x_min,
      .y = normalized_y_min,
      .width = width,
      .height = height,
  };
  ge2d_.SetCropRect(task_index_, &crop);
  return ZX_OK;
}

}  // namespace camera
