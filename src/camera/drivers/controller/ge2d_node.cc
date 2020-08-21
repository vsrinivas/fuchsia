// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/ge2d_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <algorithm>

#include <fbl/auto_call.h>

#include "src/camera/drivers/controller/graph_utils.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"

namespace camera {

constexpr auto kTag = "camera_controller_ge2d_node";

void OnGe2dFrameAvailable(void* ctx, const frame_available_info_t* info) {
  auto nonce = TRACE_NONCE();
  TRACE_DURATION("camera", "OnGe2dFrameAvailable");
  TRACE_FLOW_BEGIN("camera", "post_on_ge2d_frame_available", nonce);
  // This method is invoked by the GE2D in its own thread,
  // so the event must be marshalled to the
  // controller's thread.
  auto* ge2d_node = static_cast<Ge2dNode*>(ctx);
  ge2d_node->RunOnMainThread([ge2d_node, info = *info, nonce]() {
    TRACE_DURATION("camera", "OnGe2dFrameAvailable.task");
    TRACE_FLOW_END("camera", "post_on_ge2d_frame_available", nonce);
    ge2d_node->OnFrameAvailable(&info);
  });
}

void OnGe2dResChange(void* ctx, const frame_available_info_t* info) {
  static_cast<camera::ProcessNode*>(ctx)->OnResolutionChanged(info);
}

void OnGe2dTaskRemoved(void* ctx, task_remove_status_t status) {
  static_cast<Ge2dNode*>(ctx)->OnTaskRemoved(status);
}

fit::result<ProcessNode*, zx_status_t> Ge2dNode::CreateGe2dNode(
    const ControllerMemoryAllocator& memory_allocator, async_dispatcher_t* dispatcher,
    zx_device_t* device, const ddk::Ge2dProtocolClient& ge2d, StreamCreationData* info,
    ProcessNode* parent_node, const InternalConfigNode& internal_ge2d_node) {
  zx_status_t status = ZX_OK;
  auto& input_buffers_hlcpp = parent_node->output_buffer_collection();
  auto result = GetBuffers(memory_allocator, internal_ge2d_node, info, kTag);
  if (result.is_error()) {
    FX_LOGST(ERROR, kTag) << "Failed to get buffers";
    return fit::error(result.error());
  }

  auto output_buffers_hlcpp = std::move(result.value());

  BufferCollectionHelper output_buffer_collection_helper(output_buffers_hlcpp);
  BufferCollectionHelper input_buffer_collection_helper(input_buffers_hlcpp);

  std::vector<fuchsia_sysmem_ImageFormat_2> output_image_formats_c;
  for (auto& format : internal_ge2d_node.image_formats) {
    output_image_formats_c.push_back(GetImageFormatFromBufferCollection(
        *output_buffer_collection_helper.GetC(), format.coded_width, format.coded_height));
  }

  std::vector<fuchsia_sysmem_ImageFormat_2> input_image_formats_c;
  for (auto& format : parent_node->output_image_formats()) {
    input_image_formats_c.push_back(GetImageFormatFromBufferCollection(
        *input_buffer_collection_helper.GetC(), format.coded_width, format.coded_height));
  }

  auto ge2d_node = std::make_unique<camera::Ge2dNode>(
      dispatcher, ge2d, parent_node, internal_ge2d_node, std::move(output_buffers_hlcpp),
      info->stream_type(), info->image_format_index, internal_ge2d_node.in_place);
  if (!ge2d_node) {
    FX_LOGST(ERROR, kTag) << "Failed to create GE2D node";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // Initialize the GE2D to get a unique task index.
  uint32_t ge2d_task_index = 0;
  switch (internal_ge2d_node.ge2d_info.config_type) {
    case Ge2DConfig::GE2D_RESIZE: {
      status = ge2d.InitTaskResize(
          input_buffer_collection_helper.GetC(), output_buffer_collection_helper.GetC(),
          ge2d_node->resize_info(), input_image_formats_c.data(), output_image_formats_c.data(),
          output_image_formats_c.size(), info->image_format_index, ge2d_node->frame_callback(),
          ge2d_node->res_callback(), ge2d_node->remove_task_callback(), &ge2d_task_index);
      if (status != ZX_OK) {
        FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GE2D resize task";
        return fit::error(status);
      }
      break;
    }
    case Ge2DConfig::GE2D_WATERMARK: {
      std::vector<zx::vmo> watermark_vmos;
      for (auto watermark : internal_ge2d_node.ge2d_info.watermark) {
        size_t size;
        zx::vmo vmo;
        auto status = load_firmware(device, watermark.filename, vmo.reset_and_get_address(), &size);
        if (status != ZX_OK || size == 0) {
          FX_PLOGST(ERROR, kTag, status) << "Failed to load the watermark image";
          return fit::error(status);
        }
        watermark_vmos.push_back(std::move(vmo));
      }

      std::vector<water_mark_info> watermarks_info;
      for (uint32_t i = 0; i < internal_ge2d_node.ge2d_info.watermark.size(); i++) {
        water_mark_info info;
        info.loc_x = internal_ge2d_node.ge2d_info.watermark[i].loc_x;
        info.loc_y = internal_ge2d_node.ge2d_info.watermark[i].loc_y;
        info.wm_image_format =
            ConvertHlcppImageFormat2toCType(internal_ge2d_node.ge2d_info.watermark[i].image_format);
        info.watermark_vmo = watermark_vmos[i].release();
        constexpr float kGlobalAlpha = 200.f / 255;
        info.global_alpha = kGlobalAlpha;
        watermarks_info.push_back(info);
      }

      auto cleanup = fbl::MakeAutoCall([watermarks_info]() {
        for (auto info : watermarks_info) {
          ZX_ASSERT_MSG(ZX_OK == zx_handle_close(info.watermark_vmo),
                        "Failed to free up watermark VMOs");
        }
      });

      if (internal_ge2d_node.in_place) {
        status = ge2d.InitTaskInPlaceWaterMark(
            input_buffer_collection_helper.GetC(), watermarks_info.data(), watermarks_info.size(),
            input_image_formats_c.data(), input_image_formats_c.size(), info->image_format_index,
            ge2d_node->frame_callback(), ge2d_node->res_callback(),
            ge2d_node->remove_task_callback(), &ge2d_task_index);
      } else {
        status = ge2d.InitTaskWaterMark(
            input_buffer_collection_helper.GetC(), output_buffer_collection_helper.GetC(),
            watermarks_info.data(), watermarks_info.size(), input_image_formats_c.data(),
            input_image_formats_c.size(), info->image_format_index, ge2d_node->frame_callback(),
            ge2d_node->res_callback(), ge2d_node->remove_task_callback(), &ge2d_task_index);
      }
      if (status != ZX_OK) {
        FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GE2D watermark task";
        return fit::error(status);
      }
      break;
    }
    default: {
      FX_LOGST(ERROR, kTag) << "Unkwon config type";
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
  }

  ge2d_node->set_task_index(ge2d_task_index);

  auto return_value = fit::ok(ge2d_node.get());
  parent_node->AddChildNodeInfo(std::move(ge2d_node));
  return return_value;
}

void Ge2dNode::OnFrameAvailable(const frame_available_info_t* info) {
  ZX_ASSERT(thread_checker_.IsCreationThreadCurrent());
  TRACE_DURATION("camera", "Ge2dNode::OnFrameAvailable", "buffer_index", info->buffer_id);

  if (shutdown_requested_ || info->frame_status != FRAME_STATUS_OK) {
    return;
  }
  UpdateFrameCounterForAllChildren();

  if (NeedToDropFrame()) {
    if (!in_place_processing_) {
      ge2d_.ReleaseFrame(task_index_, info->buffer_id);
    }
    parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
    return;
  }
  // Free up parent's frame.
  if (!in_place_processing_) {
    parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
  }
  ProcessNode::OnFrameAvailable(info);
}

void Ge2dNode::OnReleaseFrame(uint32_t buffer_index) {
  TRACE_DURATION("camera", "Ge2dNode::OnReleaseFrame", "buffer_index", buffer_index);
  fbl::AutoLock guard(&in_use_buffer_lock_);
  ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
  in_use_buffer_count_[buffer_index]--;
  if (in_use_buffer_count_[buffer_index] != 0 || shutdown_requested_) {
    return;
  }
  if (in_place_processing_) {
    parent_node_->OnReleaseFrame(buffer_index);
    return;
  }
  ge2d_.ReleaseFrame(task_index_, buffer_index);
}

void Ge2dNode::OnReadyToProcess(const frame_available_info_t* info) {
  TRACE_DURATION("camera", "Ge2dNode::OnReadyToProcess");
  TRACE_FLOW_BEGIN("camera", "ge2d_node_on_ready_to_process", info->buffer_id);
  async::PostTask(dispatcher_, [this, buffer_index = info->buffer_id]() {
    TRACE_DURATION("camera", "Ge2dNode::OnReadyToProcess.task");
    TRACE_FLOW_END("camera", "ge2d_node_on_ready_to_process", buffer_index);
    if (enabled_) {
      ZX_ASSERT(ZX_OK == ge2d_.ProcessFrame(task_index_, buffer_index));
    } else {
      // Since streaming is disabled the incoming frame is released
      // so it gets added back to the pool.
      parent_node_->OnReleaseFrame(buffer_index);
    }
  });
}

void Ge2dNode::OnTaskRemoved(zx_status_t status) {
  ZX_ASSERT(status == ZX_OK);

  async::PostTask(dispatcher_, [this]() {
    node_callback_received_ = true;
    OnCallbackReceived();
  });
}

void Ge2dNode::OnShutdown(fit::function<void(void)> shutdown_callback) {
  shutdown_callback_ = std::move(shutdown_callback);

  // After a shutdown request has been made,
  // no other calls should be made to the GE2D driver.
  shutdown_requested_ = true;

  // Request GE2D to shutdown.
  ge2d_.RemoveTask(task_index_);

  auto child_shutdown_completion_callback = [this]() {
    child_node_callback_received_ = true;
    OnCallbackReceived();
  };

  ZX_ASSERT_MSG(configured_streams().size() == 1,
                "Cannot shutdown a stream which supports multiple streams");

  // Forward the shutdown request to child node.
  child_nodes().at(0)->OnShutdown(child_shutdown_completion_callback);
}

void Ge2dNode::OnResolutionChangeRequest(uint32_t output_format_index) {
  if (enabled_) {
    TRACE_DURATION("camera", "Ge2dNode::OnResolutionChangeRequest", "index", output_format_index);
    if (task_type_ == Ge2DConfig::GE2D_WATERMARK) {
      ge2d_.SetInputAndOutputResolution(task_index_, output_format_index);
    } else {
      ge2d_.SetOutputResolution(task_index_, output_format_index);
    }
    set_current_image_format_index(output_format_index);
  }
}

zx_status_t Ge2dNode::OnSetCropRect(float x_min, float y_min, float x_max, float y_max) {
  TRACE_DURATION("camera", "Ge2dNode::OnSetCropRect");
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

  auto& input_image_format = parent_node()->output_image_formats().at(0);
  auto normalized_x_min = static_cast<uint32_t>(x_min * input_image_format.coded_width + 0.5f);
  auto normalized_y_min = static_cast<uint32_t>(y_min * input_image_format.coded_height + 0.5f);
  auto normalized_x_max = static_cast<uint32_t>(x_max * input_image_format.coded_width + 0.5f);
  auto normalized_y_max = static_cast<uint32_t>(y_max * input_image_format.coded_height + 0.5f);

  if (enabled_) {
    auto width = normalized_x_max - normalized_x_min;
    auto height = normalized_y_max - normalized_y_min;

    rect_t crop = {
        .x = normalized_x_min,
        .y = normalized_y_min,
        .width = width,
        .height = height,
    };
    ge2d_.SetCropRect(task_index_, &crop);
  }
  return ZX_OK;
}

}  // namespace camera
