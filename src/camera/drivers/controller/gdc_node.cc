// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/gdc_node.h"

#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <safemath/safe_conversions.h>

#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/lib/format_conversion/format_conversion.h"
#include "src/devices/lib/sysmem/sysmem.h"

namespace camera {

constexpr auto kTag = "camera_controller_gdc_node";

static fpromise::result<gdc_config_info, zx_status_t> LoadGdcConfiguration(
    const LoadFirmwareCallback& load_firmware, ProductConfig& product_config,
    const camera::GdcConfig& config_type) {
  if (config_type == GdcConfig::INVALID) {
    FX_LOGST(DEBUG, kTag) << "Invalid GDC configuration type";
    return fpromise::error(ZX_ERR_INVALID_ARGS);
  }
  auto result = load_firmware(product_config.GetGdcConfigFile(config_type));
  if (result.is_error() || result.value().second == 0) {
    FX_PLOGST(ERROR, kTag, result.error()) << "Failed to load the GDC firmware";
    return fpromise::error(result.error());
  }
  auto [vmo, size] = result.take_value();
  return fpromise::ok(
      gdc_config_info{.config_vmo = vmo.release(), .size = safemath::checked_cast<uint32_t>(size)});
}

GdcNode::GdcNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
                 FrameCallback frame_callback, const ddk::GdcProtocolClient& gdc)
    : ProcessNode(dispatcher, NodeType::kGe2d, attachments, std::move(frame_callback)), gdc_(gdc) {}

fpromise::result<std::unique_ptr<ProcessNode>, zx_status_t> GdcNode::Create(
    async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
    const LoadFirmwareCallback& load_firmware, const ddk::GdcProtocolClient& gdc,
    const InternalConfigNode& internal_gdc_node, const StreamCreationData& info) {
  TRACE_DURATION("camera", "GdcNode::Create");

  // Create GDC Node
  auto node =
      std::make_unique<camera::GdcNode>(dispatcher, attachments, std::move(frame_callback), gdc);

  fuchsia_sysmem::wire::BufferCollectionInfo2 output_buffer_collection =
      ConvertToWireTypeBufferCollectionInfo2(node->OutputBuffers());
  fuchsia_sysmem::wire::BufferCollectionInfo2 input_buffer_collection =
      ConvertToWireTypeBufferCollectionInfo2(node->InputBuffers());

  // Convert the formats to C type
  std::vector<image_format_2_t> output_image_formats_c;
  for (const auto& format : internal_gdc_node.image_formats) {
    image_format_2_t value;
    auto original = GetImageFormatFromBufferCollection(output_buffer_collection, format.coded_width,
                                                       format.coded_height);
    sysmem::image_format_2_banjo_from_fidl(original, value);
    output_image_formats_c.push_back(value);
  }

  // GDC only supports one input format and multiple output format at the
  // moment. So we take the first format from the previous node.
  // All existing usecases we support have only 1 format going into GDC.
  auto input_image_formats_c = GetImageFormatFromBufferCollection(
      input_buffer_collection, node->InputFormats()[0].coded_width,
      node->InputFormats()[0].coded_height);

  // Image format index refers to the final output format list of the pipeline. If this GDC node
  // only has one output format, then the image format index must not be meant for this node. If
  // this assumption is false the GdcTask will fail to init. Without this filter then streams which
  // have a multiple output resolution node, but only a single output resolution GDC node will fail
  // to init.
  //
  // Note: this is highly specific to sherlock use case. It would be good to revisit how nodes
  // receive their output format index when this assumption doesn't hold on another platform.
  uint32_t output_format_index =
      internal_gdc_node.image_formats.size() > 1 ? info.image_format_index : 0;

  // Get the GDC configurations loaded
  auto product_config = ProductConfig::Create();
  std::vector<gdc_config_info> config_vmos_info;
  for (const auto& config : internal_gdc_node.gdc_info.config_type) {
    auto gdc_config = LoadGdcConfiguration(load_firmware, *product_config, config);
    if (gdc_config.is_error()) {
      FX_LOGST(ERROR, kTag) << "Failed to load GDC configuration";
      return fpromise::error(gdc_config.error());
    }
    config_vmos_info.push_back(gdc_config.value());
  }

  auto cleanup = fit::defer([config_vmos_info]() {
    for (auto info : config_vmos_info) {
      ZX_ASSERT_MSG(ZX_OK == zx_handle_close(info.config_vmo), "Failed to free up Config VMOs");
    }
  });

  // Initialize the GDC to get a unique task index
  buffer_collection_info_2 temp_input_collection, temp_output_collection;
  image_format_2_t temp_image_format;
  sysmem::buffer_collection_info_2_banjo_from_fidl(std::move(input_buffer_collection),
                                                   temp_input_collection);
  sysmem::buffer_collection_info_2_banjo_from_fidl(std::move(output_buffer_collection),
                                                   temp_output_collection);
  sysmem::image_format_2_banjo_from_fidl(input_image_formats_c, temp_image_format);
  auto status =
      gdc.InitTask(&temp_input_collection, &temp_output_collection, &temp_image_format,
                   output_image_formats_c.data(), output_image_formats_c.size(),
                   output_format_index, config_vmos_info.data(), config_vmos_info.size(),
                   node->GetHwFrameReadyCallback(), node->GetHwFrameResolutionChangeCallback(),
                   node->GetHwTaskRemovedCallback(), &node->task_index_);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GDC";
    return fpromise::error(status);
  }

  return fpromise::ok(std::move(node));
}

void GdcNode::ProcessFrame(FrameToken token, frame_metadata_t metadata) {
  TRACE_DURATION("camera", "GdcNode::ProcessFrame", "buffer_index", *token);
  if (shutdown_callback_) {
    // ~token
    return;
  }

  input_frame_queue_.push(token);
  ZX_ASSERT(gdc_.ProcessFrame(task_index_, *token, metadata.capture_timestamp) == ZX_OK);
}

void GdcNode::SetOutputFormat(uint32_t output_format_index, fit::closure callback) {
  TRACE_DURATION("camera", "GdcNode::SetOutputFormat", "format_index", output_format_index);
  format_callback_ = std::move(callback);
  gdc_.SetOutputResolution(task_index_, output_format_index);
}

void GdcNode::ShutdownImpl(fit::closure callback) {
  TRACE_DURATION("camera", "GdcNode::ShutdownImpl");
  ZX_ASSERT(!shutdown_callback_);
  shutdown_callback_ = std::move(callback);

  // Request GDC to shutdown.
  gdc_.RemoveTask(task_index_);
}

void GdcNode::HwFrameReady(frame_available_info_t info) {
  TRACE_DURATION("camera", "GdcNode::HwFrameReady", "status", info.frame_status, "buffer_index",
                 info.buffer_id);
  auto token = std::move(input_frame_queue_.front());
  input_frame_queue_.pop();

  // Don't do anything further with error frames.
  if (info.frame_status != FRAME_STATUS_OK) {
    FX_LOGST(ERROR, kTag) << "failed gdc frame: " << static_cast<uint32_t>(info.frame_status);
    return;
  }

  // Send the frame onward.
  SendFrame(info.buffer_id, info.metadata, [this, buffer_index = info.buffer_id] {
    gdc_.ReleaseFrame(task_index_, buffer_index);
  });
}

void GdcNode::HwFrameResolutionChanged(frame_available_info_t info) {
  TRACE_DURATION("camera", "GdcNode::HwFrameResolutionChanged");
  format_callback_();
  format_callback_ = nullptr;
}

void GdcNode::HwTaskRemoved(task_remove_status_t status) {
  TRACE_DURATION("camera", "GdcNode::HwTaskRemoved");
  ZX_ASSERT(status == TASK_REMOVE_STATUS_OK);
  ZX_ASSERT(shutdown_callback_);
  if (!input_frame_queue_.empty()) {
    FX_LOGS(WARNING) << "GDC driver completed task removal but did not complete processing for all "
                        "frames it was sent. These will be manually released.";
    while (!input_frame_queue_.empty()) {
      input_frame_queue_.pop();
    }
  }
  shutdown_callback_();
}

}  // namespace camera
