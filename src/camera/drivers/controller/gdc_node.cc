// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/gdc_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/auto_call.h>

#include "src/camera/drivers/controller/graph_utils.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"

namespace camera {

constexpr auto kTag = "camera_controller_gdc_node";

const char* ToConfigFileName(const camera::GdcConfig& config_type) {
  switch (config_type) {
    case GdcConfig::MONITORING_360p:
      return "config_1152x1440_to_512x384_Crop_Rotate.bin";
    case GdcConfig::MONITORING_480p:
      return "config_1152x1440_to_720x540_Crop_Rotate.bin";
    case GdcConfig::MONITORING_720p:
      return "config_1152x1440_to_1152x864_Crop_Rotate.bin";
    case GdcConfig::MONITORING_ML:
      return "config_001_2176x2720-to-640x512-RS-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE:
      return "config_002_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_EXTENDED_FOV:
      return "config_003_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_ML:
      return "config_001_2240x1792-to-640x512-S-YUV420SemiPlanar.bin";
    case GdcConfig::INVALID:
    default:
      return nullptr;
  }
}

fit::result<gdc_config_info, zx_status_t> LoadGdcConfiguration(
    zx_device_t* device, const camera::GdcConfig& config_type) {
  if (config_type == GdcConfig::INVALID) {
    FX_LOGST(DEBUG, kTag) << "Invalid GDC configuration type";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  gdc_config_info info;
  size_t size;
  auto status = load_firmware(device, ToConfigFileName(config_type), &info.config_vmo, &size);
  if (status != ZX_OK || size == 0) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to load the GDC firmware";
    return fit::error(status);
  }
  info.size = size;
  return fit::ok(info);
}

void OnGdcFrameAvailable(void* ctx, const frame_available_info_t* info) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "OnGdcFrameAvailable");
  TRACE_FLOW_BEGIN("camera", "post_gdc_frame_available", nonce);
  // This method is invoked by the GDC in its own thread,
  // so the event must be marshalled to the
  // controller's thread.
  auto* gdc_node = static_cast<GdcNode*>(ctx);
  gdc_node->RunOnMainThread([gdc_node, nonce, info = *info]() {
    TRACE_DURATION("camera", "OnGdcFrameAvailable.task");
    TRACE_FLOW_END("camera", "post_gdc_frame_available", nonce);
    gdc_node->OnFrameAvailable(&info);
  });
}

void OnGdcResChange(void* ctx, const frame_available_info_t* info) {
  static_cast<camera::ProcessNode*>(ctx)->OnResolutionChanged(info);
}

fit::result<ProcessNode*, zx_status_t> GdcNode::CreateGdcNode(
    const ControllerMemoryAllocator& memory_allocator, async_dispatcher_t* dispatcher,
    zx_device_t* device, const ddk::GdcProtocolClient& gdc, StreamCreationData* info,
    ProcessNode* parent_node, const InternalConfigNode& internal_gdc_node) {
  auto& input_buffers_hlcpp = parent_node->output_buffer_collection();
  auto result = GetBuffers(memory_allocator, internal_gdc_node, info, kTag);
  if (result.is_error()) {
    FX_LOGST(ERROR, kTag) << "Failed to get buffers";
    return fit::error(result.error());
  }

  auto output_buffers_hlcpp = std::move(result.value());

  BufferCollectionHelper output_buffer_collection_helper(output_buffers_hlcpp);
  BufferCollectionHelper input_buffer_collection_helper(input_buffers_hlcpp);

  // Convert the formats to C type
  std::vector<fuchsia_sysmem_ImageFormat_2> output_image_formats_c;
  for (const auto& format : internal_gdc_node.image_formats) {
    output_image_formats_c.push_back(GetImageFormatFromBufferCollection(
        *output_buffer_collection_helper.GetC(), format.coded_width, format.coded_height));
  }

  // GDC only supports one input format and multiple output format at the
  // moment. So we take the first format from the previous node.
  // All existing usecases we support have only 1 format going into GDC.
  auto input_image_formats_c = GetImageFormatFromBufferCollection(
      *input_buffer_collection_helper.GetC(), parent_node->output_image_formats()[0].coded_width,
      parent_node->output_image_formats()[0].coded_height);

  // Get the GDC configurations loaded
  std::vector<gdc_config_info> config_vmos_info;
  for (const auto& config : internal_gdc_node.gdc_info.config_type) {
    auto gdc_config = LoadGdcConfiguration(device, config);
    if (gdc_config.is_error()) {
      FX_LOGST(ERROR, kTag) << "Failed to load GDC configuration";
      return fit::error(gdc_config.error());
    }
    config_vmos_info.push_back(gdc_config.value());
  }

  auto cleanup = fbl::MakeAutoCall([config_vmos_info]() {
    for (auto info : config_vmos_info) {
      ZX_ASSERT_MSG(ZX_OK == zx_handle_close(info.config_vmo), "Failed to free up Config VMOs");
    }
  });

  // Create GDC Node
  auto gdc_node = std::make_unique<camera::GdcNode>(dispatcher, gdc, parent_node, internal_gdc_node,
                                                    std::move(output_buffers_hlcpp),
                                                    info->stream_type(), info->image_format_index);
  if (!gdc_node) {
    FX_LOGST(ERROR, kTag) << "Failed to create GDC node";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // Initialize the GDC to get a unique task index
  uint32_t gdc_task_index;
  auto status =
      gdc.InitTask(input_buffer_collection_helper.GetC(), output_buffer_collection_helper.GetC(),
                   &input_image_formats_c, output_image_formats_c.data(),
                   output_image_formats_c.size(), info->image_format_index, config_vmos_info.data(),
                   config_vmos_info.size(), gdc_node->frame_callback(), gdc_node->res_callback(),
                   gdc_node->remove_task_callback(), &gdc_task_index);
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GDC";
    return fit::error(status);
  }

  gdc_node->set_task_index(gdc_task_index);

  // Add child node.
  auto return_value = fit::ok(gdc_node.get());
  parent_node->AddChildNodeInfo(std::move(gdc_node));
  return return_value;
}

void GdcNode::OnFrameAvailable(const frame_available_info_t* info) {
  ZX_ASSERT(thread_checker_.IsCreationThreadCurrent());
  TRACE_DURATION("camera", "GdcNode::OnFrameAvailable", "buffer_index", info->buffer_id);
  if (shutdown_requested_ || info->frame_status != FRAME_STATUS_OK) {
    return;
  }

  UpdateFrameCounterForAllChildren();

  if (NeedToDropFrame()) {
    parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
    gdc_.ReleaseFrame(task_index_, info->buffer_id);
    return;
  }
  // Free up parent's frame.
  parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
  ProcessNode::OnFrameAvailable(info);
}

void GdcNode::OnReleaseFrame(uint32_t buffer_index) {
  TRACE_DURATION("camera", "GdcNode::OnReleaseFrame", "buffer_index", buffer_index);
  fbl::AutoLock al(&in_use_buffer_lock_);
  ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
  in_use_buffer_count_[buffer_index]--;
  if (in_use_buffer_count_[buffer_index] != 0) {
    return;
  }
  if (!shutdown_requested_) {
    gdc_.ReleaseFrame(task_index_, buffer_index);
  }
}

void GdcNode::OnReadyToProcess(const frame_available_info_t* info) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "GdcNode::OnReadyToProcess");
  TRACE_FLOW_BEGIN("camera", "post_process_frame", nonce);
  async::PostTask(dispatcher_, [this, nonce, buffer_index = info->buffer_id]() {
    TRACE_DURATION("camera", "GdcNode::OnReadyToProcess.task", "buffer_index", buffer_index);
    TRACE_FLOW_END("camera", "post_process_frame", nonce);
    if (enabled_) {
      ZX_ASSERT(ZX_OK == gdc_.ProcessFrame(task_index_, buffer_index));
    } else {
      // Since streaming is disabled the incoming frame is released
      // so it gets added back to the pool.
      parent_node_->OnReleaseFrame(buffer_index);
    }
  });
}

void GdcNode::OnTaskRemoved(zx_status_t status) {
  ZX_ASSERT(status == ZX_OK);
  async::PostTask(dispatcher_, [this]() {
    node_callback_received_ = true;
    OnCallbackReceived();
  });
}

void GdcNode::OnShutdown(fit::function<void(void)> shutdown_callback) {
  shutdown_callback_ = std::move(shutdown_callback);

  // After a shutdown request has been made,
  // no other calls should be made to the GDC driver.
  shutdown_requested_ = true;

  // Request GDC to shutdown.
  gdc_.RemoveTask(task_index_);

  auto child_shutdown_completion_callback = [this]() {
    child_node_callback_received_ = true;
    OnCallbackReceived();
  };

  ZX_ASSERT_MSG(configured_streams().size() == 1,
                "Cannot shutdown a stream which supports multiple streams");

  // Forward the shutdown request to child node.
  child_nodes().at(0)->OnShutdown(child_shutdown_completion_callback);
}

void GdcNode::OnResolutionChangeRequest(uint32_t output_format_index) {
  if (enabled_) {
    TRACE_DURATION("camera", "GdcNode::OnResolutionChangeRequest", "index", output_format_index);
    gdc_.SetOutputResolution(task_index_, output_format_index);
    set_current_image_format_index(output_format_index);
  }
}
}  // namespace camera
