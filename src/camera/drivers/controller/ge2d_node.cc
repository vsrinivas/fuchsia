// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/ge2d_node.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include <fbl/auto_call.h>

#include "src/camera/drivers/controller/graph_utils.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto kTag = "camera_controller_ge2d_node";

void OnGe2dFrameAvailable(void* ctx, const frame_available_info_t* info) {
  static_cast<camera::Ge2dNode*>(ctx)->OnFrameAvailable(info);
}

// TODO(41730): Implement this.
void OnGe2dResChange(void* ctx, const frame_available_info_t* info) {}

void OnGe2dTaskRemoved(void* ctx, task_remove_status_t status) {
  static_cast<Ge2dNode*>(ctx)->OnTaskRemoved(status);
}

fit::result<ProcessNode*, zx_status_t> Ge2dNode::CreateGe2dNode(
    ControllerMemoryAllocator& memory_allocator, async_dispatcher_t* dispatcher,
    zx_device_t* device, const ddk::Ge2dProtocolClient& ge2d, StreamCreationData* info,
    ProcessNode* parent_node, const InternalConfigNode& internal_ge2d_node) {
  auto& input_buffers_hlcpp = parent_node->output_buffer_collection();
  auto result = GetBuffers(memory_allocator, internal_ge2d_node, info, parent_node);
  if (result.is_error()) {
    FX_LOGST(ERROR, kTag) << "Failed to get buffers";
    return fit::error(result.error());
  }

  auto output_buffers_hlcpp = std::move(result.value());

  BufferCollectionHelper output_buffer_collection_helper(output_buffers_hlcpp);
  BufferCollectionHelper input_buffer_collection_helper(input_buffers_hlcpp);

  // Convert the formats to C type
  std::vector<fuchsia_sysmem_ImageFormat_2> output_image_formats_c;
  for (uint32_t i = 0; i < internal_ge2d_node.image_formats.size(); i++) {
    auto image_format_hlcpp = internal_ge2d_node.image_formats[i];
    output_image_formats_c.push_back(ConvertHlcppImageFormat2toCType(&image_format_hlcpp));
  }

  fuchsia_sysmem_ImageFormat_2 input_image_formats_c;
  switch (internal_ge2d_node.ge2d_info.config_type) {
    case Ge2DConfig::GE2D_RESIZE: {
      // In this case the input format index is the same as previous nodes first
      // image format.
      input_image_formats_c =
          ConvertHlcppImageFormat2toCType(&parent_node->output_image_formats()[0]);

      break;
    }
    case Ge2DConfig::GE2D_WATERMARK: {
      // In this case the input format index is the one requested by client.
      input_image_formats_c = ConvertHlcppImageFormat2toCType(
          &parent_node->output_image_formats()[info->image_format_index]);

      // TODO(braval): Load Watermark VMO here.
      break;
    }
      // clang-format off
    default: {
      FX_LOGST(ERROR, kTag) << "Unkwon config type";
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
      // clang-format on
  }

  // Create GE2D Node.
  auto ge2d_node = std::make_unique<camera::Ge2dNode>(
      dispatcher, ge2d, parent_node, internal_ge2d_node.image_formats,
      std::move(output_buffers_hlcpp), info->stream_config->properties.stream_type(),
      internal_ge2d_node.supported_streams, internal_ge2d_node.output_frame_rate,
      internal_ge2d_node.ge2d_info.resize);
  if (!ge2d_node) {
    FX_LOGST(ERROR, kTag) << "Failed to create GE2D node";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // Initialize the GE2D to get a unique task index.
  uint32_t ge2d_task_index = 0;
  switch (internal_ge2d_node.ge2d_info.config_type) {
    case Ge2DConfig::GE2D_RESIZE: {
      auto status = ge2d.InitTaskResize(
          input_buffer_collection_helper.GetC(), output_buffer_collection_helper.GetC(),
          ge2d_node->resize_info(), &input_image_formats_c, output_image_formats_c.data(),
          output_image_formats_c.size(), info->image_format_index, ge2d_node->frame_callback(),
          ge2d_node->res_callback(), ge2d_node->remove_task_callback(), &ge2d_task_index);
      if (status != ZX_OK) {
        FX_PLOGST(ERROR, kTag, status) << "Failed to initialize GE2D resize task";
        return fit::error(status);
      }

      break;
    }
    case Ge2DConfig::GE2D_WATERMARK: {
      // TODO(braval): Implement this.
      break;
    }
    default: {
      FX_LOGST(ERROR, kTag) << "Unkwon config type";
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
  }

  ge2d_node->set_task_index(ge2d_task_index);

  // Add child node.
  auto return_value = fit::ok(ge2d_node.get());
  parent_node->AddChildNodeInfo(std::move(ge2d_node));
  return return_value;
}

void Ge2dNode::OnFrameAvailable(const frame_available_info_t* info) {
  // Once shutdown is requested no calls should be made to the driver.
  if (!shutdown_requested_) {
    UpdateFrameCounterForAllChildren();

    if (NeedToDropFrame()) {
      parent_node_->OnReleaseFrame(info->metadata.input_buffer_index);
      ge2d_.ReleaseFrame(task_index_, info->buffer_id);
    } else {
      ProcessNode::OnFrameAvailable(info);
    }
  }
}

void Ge2dNode::OnReleaseFrame(uint32_t buffer_index) {
  fbl::AutoLock guard(&in_use_buffer_lock_);
  ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
  in_use_buffer_count_[buffer_index]--;
  if (in_use_buffer_count_[buffer_index] != 0) {
    return;
  }
  if (!shutdown_requested_) {
    ge2d_.ReleaseFrame(task_index_, buffer_index);
  }
}

void Ge2dNode::OnReadyToProcess(const frame_available_info_t* info) {
  fbl::AutoLock guard(&event_queue_lock_);
  event_queue_.emplace([this, buffer_index = info->buffer_id]() {
    if (enabled_) {
      ZX_ASSERT(ZX_OK == ge2d_.ProcessFrame(task_index_, buffer_index));
    } else {
      // Since streaming is disabled the incoming frame is released
      // so it gets added back to the pool.
      parent_node_->OnReleaseFrame(buffer_index);
    }
    fbl::AutoLock guard(&event_queue_lock_);
    event_queue_.pop();
  });
  event_queue_.back().Post(dispatcher_);
}

void Ge2dNode::OnTaskRemoved(zx_status_t status) {
  ZX_ASSERT(status == ZX_OK);
  fbl::AutoLock guard(&event_queue_lock_);
  event_queue_.emplace([this]() {
    node_callback_received_ = true;
    fbl::AutoLock guard(&event_queue_lock_);
    event_queue_.pop();
    OnCallbackReceived();
  });
  event_queue_.back().Post(dispatcher_);
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

}  // namespace camera
