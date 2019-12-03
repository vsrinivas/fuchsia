// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/camera_manager2/video_device_client.h"

#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <sstream>
#include <utility>
#include <vector>

#include <src/lib/syslog/cpp/logger.h>
namespace camera {

std::unique_ptr<VideoDeviceClient> VideoDeviceClient::Create(
    fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller) {
  if (!controller.is_valid()) {
    FX_LOGS(ERROR) << __func__ << " Received invalid InterfaceHandle.";
    return nullptr;
  }

  std::unique_ptr<VideoDeviceClient> device(new VideoDeviceClient);
  device->camera_control_.Bind(std::move(controller));

  // Since the interface is synchronous, just gather info here.
  // TODO(41395): Handle the stalled driver scenario so that one bad device
  // doesn't hose the manager.
  if (device->GetInitialInfo() != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't get configs or info for device";
    return nullptr;
  }

  return device;
}

zx_status_t VideoDeviceClient::CreateStream(
    uint32_t config_index, uint32_t stream_type, uint32_t image_format_index,
    fidl::InterfaceHandle<fuchsia::sysmem::BufferCollection> sysmem_collection,
    ::fidl::InterfaceRequest<::fuchsia::camera2::Stream> stream) {
  if (config_index >= configs_.size()) {
    FX_LOGS(ERROR) << "Requested config " << config_index << " Does not exist.";
    return ZX_ERR_INVALID_ARGS;
  }
  if (stream_type >= configs_[config_index].stream_configs.size()) {
    FX_LOGS(ERROR) << "Requested  stream " << stream_type << " of config " << config_index
                   << " Does not exist.";
    return ZX_ERR_INVALID_ARGS;
  }
  if (image_format_index >=
      configs_[config_index].stream_configs[stream_type].image_formats.size()) {
    FX_LOGS(ERROR) << "Requested image format " << image_format_index << " of stream "
                   << stream_type << " of config " << config_index << " Does not exist.";
    return ZX_ERR_INVALID_ARGS;
  }
  if (!sysmem_collection.is_valid()) {
    FX_LOGS(ERROR) << __func__ << " Received invalid InterfaceHandle for buffer collection.";
    return ZX_ERR_INVALID_ARGS;
  }
  auto sysmem_collection_ptr = sysmem_collection.BindSync();
  auto &stream_config = configs_[config_index].stream_configs[stream_type];
  zx_status_t status = sysmem_collection_ptr->SetConstraints(true, stream_config.constraints);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to connect to SetConstraints.";
    return status;
  }

  zx_status_t allocation_status = ZX_OK;
  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection_info{};
  FX_LOGS(INFO) << "Waiting for buffers to be allocated.";
  status =
      sysmem_collection_ptr->WaitForBuffersAllocated(&allocation_status, &buffer_collection_info);
  if (allocation_status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to allocate buffers.";
    return allocation_status;
  }
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to call WaitForBuffersAllocated.";
    return status;
  }

  FX_LOGS(INFO) << "Buffers are allocated.";
  status = camera_control_->CreateStream(config_index, stream_type, image_format_index,
                                         std::move(buffer_collection_info), std::move(stream));
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to call CreateStream.";
    return status;
  }

  // Close any buffer collections related to streams that exist from other configs.
  // Do this after the stream is set up, because the driver will be the one closing
  // the stream channel.
  // Also, for now close any stream with the same config and stream type:
  auto should_remove = [config_index, stream_type](const Stream &stream) {
    return stream.config_index != config_index ||
           (stream.config_index == config_index && stream.stream_type == stream_type);
  };
  // Call Close() on the streams first:
  for (auto &stream : open_streams_) {
    if (should_remove(stream) && stream.sysmem_collection.is_bound()) {
      FX_LOGS(INFO) << "Closing previously active stream with config " << stream.config_index
                    << " and stream type " << stream.stream_type;
      stream.sysmem_collection->Close();
    }
  }
  open_streams_.remove_if(should_remove);

  // Finally, add the newly created stream to open_streams_:
  open_streams_.push_back({config_index, stream_type, std::move(sysmem_collection_ptr)});
  return ZX_OK;
}

zx_status_t VideoDeviceClient::MatchConstraints(
    const fuchsia::camera2::StreamConstraints &constraints, uint32_t *config_index,
    uint32_t *stream_type) {
  // match first stream that has same stream type:
  if (!constraints.has_properties() || !constraints.properties().has_stream_type()) {
    FX_LOGS(ERROR) << "Constraints did not contain a stream type.";
    return ZX_ERR_INVALID_ARGS;
  }

  auto requested_stream_type = constraints.properties().stream_type();
  std::vector<std::pair<uint32_t, uint32_t>> matches;
  for (uint32_t i = 0; i < configs_.size(); ++i) {
    auto &config = configs_[i];
    for (uint32_t j = 0; j < config.stream_configs.size(); ++j) {
      auto &stream = config.stream_configs[j];
      if (stream.properties.has_stream_type() &&
          stream.properties.stream_type() == requested_stream_type) {
        if (!constraints.has_format_index() ||
            constraints.format_index() < stream.image_formats.size()) {
          matches.emplace_back(i, j);
        }
      }
    }
  }

  if (matches.empty()) {
    FX_LOGS(ERROR) << "Stream type " << static_cast<uint32_t>(requested_stream_type)
                   << " unsupported";
    return ZX_ERR_NO_RESOURCES;
  }

  if (matches.size() > 1) {
    std::stringstream ss;
    ss << "Driver reported multiple streams of same type "
       << static_cast<uint32_t>(requested_stream_type) << ":";
    for (const auto &match : matches) {
      ss << "\n  config " << match.first << ", stream " << match.second;
    }
    FX_LOGS(ERROR) << ss.str();
    return ZX_ERR_INTERNAL;
  }

  *config_index = matches[0].first;
  *stream_type = matches[0].second;

  return ZX_OK;
}

zx_status_t VideoDeviceClient::GetInitialInfo() {
  zx_status_t out_status;
  fidl::VectorPtr<fuchsia::camera2::hal::Config> out_configs;
  zx_status_t fidl_status = camera_control_->GetConfigs(&out_configs, &out_status);

  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't get Camera Configs. fidl status: " << fidl_status;
    return fidl_status;
  }
  if (out_status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't get Camera Configs. status: " << out_status;
    return out_status;
  }
  if (!out_configs) {
    FX_LOGS(ERROR) << "Couldn't get Camera Configs. No Configs.";
    return ZX_ERR_INTERNAL;
  }

  // Check the configs for validity:
  // 1) The configs vector must not be empty.
  // 2) Each config must have at least one stream_config.
  // 3) Each stream must have a stream_type in properties.
  // 4) Each stream must have at least one image_format.
  if (out_configs->empty()) {
    FX_LOGS(ERROR) << "Couldn't get Camera Configs. Empty set of configs received.";
    return ZX_ERR_INTERNAL;
  }

  uint32_t config_index = 0;
  for (auto &config : configs_) {
    if (config.stream_configs.empty()) {
      FX_LOGS(ERROR) << "Error with Camera Configs. Config " << config_index << " has no streams.";
      return ZX_ERR_INTERNAL;
    }
    uint32_t stream_index = 0;
    for (auto &stream : config.stream_configs) {
      if (!stream.properties.has_stream_type()) {
        FX_LOGS(ERROR) << "Error with Camera Configs. Config " << config_index << ", stream "
                       << stream_index << " has no properties.";
        return ZX_ERR_INTERNAL;
      }
      if (stream.image_formats.empty()) {
        FX_LOGS(ERROR) << "Error with Camera Configs. Config " << config_index << ", stream "
                       << stream_index << " has no image formats.";
        return ZX_ERR_INTERNAL;
      }
      ++stream_index;
    }
    ++config_index;
  }

  // now we have configs, copy the vector to member variable.
  configs_ = std::move(out_configs.value());

  fidl_status = camera_control_->GetDeviceInfo(&device_info_);
  if (fidl_status != ZX_OK) {
    FX_LOGS(ERROR) << "Couldn't get device info for device ";
    return fidl_status;
  }
  return ZX_OK;
}

}  // namespace camera
