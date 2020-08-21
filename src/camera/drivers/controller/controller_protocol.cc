// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller_protocol.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include <fbl/auto_call.h>

namespace camera {

namespace {
constexpr auto kTag = "camera_controller";
}  // namespace

ControllerImpl::ControllerImpl(zx_device_t* device,
                               fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> control,
                               async_dispatcher_t* dispatcher, const ddk::IspProtocolClient& isp,
                               const ddk::GdcProtocolClient& gdc,
                               const ddk::Ge2dProtocolClient& ge2d,
                               fit::closure on_connection_closed,
                               fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator,
                               const zx::event& shutdown_event)
    : binding_(this),
      pipeline_manager_(device, dispatcher, isp, gdc, ge2d, std::move(sysmem_allocator),
                        shutdown_event) {
  binding_.set_error_handler(
      [occ = std::move(on_connection_closed)](zx_status_t /*status*/) { occ(); });
  PopulateConfigurations();
  binding_.Bind(std::move(control), dispatcher);
}

zx_status_t ControllerImpl::GetInternalConfiguration(uint32_t config_index,
                                                     InternalConfigInfo** internal_config) {
  if (config_index >= internal_configs_.configs_info.size() || internal_config == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }

  *internal_config = &internal_configs_.configs_info[config_index];
  return ZX_OK;
}

void ControllerImpl::GetNextConfig(GetNextConfigCallback callback) {
  fuchsia::camera2::hal::Config config;

  if (config_count_ >= configs_.size()) {
    callback(nullptr, ZX_ERR_STOP);
    return;
  }
  config = fidl::Clone(configs_.at(config_count_));
  callback(fidl::MakeOptional(std::move(config)), ZX_OK);
  config_count_++;
}

static InternalConfigNode* GetStreamConfigNode(
    InternalConfigInfo* internal_config, fuchsia::camera2::CameraStreamType stream_config_type) {
  // Internal API, assuming the pointer will be valid always.
  for (auto& stream_info : internal_config->streams_info) {
    auto supported_streams = stream_info.supported_streams;
    if (std::any_of(supported_streams.begin(), supported_streams.end(),
                    [stream_config_type](auto& supported_stream) {
                      return supported_stream.type == stream_config_type;
                    })) {
      return &stream_info;
    }
  }
  return nullptr;
}

void ControllerImpl::CreateStream(uint32_t config_index, uint32_t stream_index,
                                  uint32_t image_format_index,
                                  fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection,
                                  fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  TRACE_DURATION("camera", "ControllerImpl::CreateStream");
  zx_status_t status = ZX_OK;
  auto cleanup = fbl::MakeAutoCall([&stream, &status]() { stream.Close(status); });
  auto external_configs = camera::SherlockExternalConfigs();

  // Input Validations
  if (config_index >= configs_.size()) {
    FX_LOGST(DEBUG, kTag) << "Invalid config index " << config_index;
    status = ZX_ERR_INVALID_ARGS;
    return;
  }
  const auto& config = configs_[config_index];

  if (stream_index >= config.stream_configs.size()) {
    FX_LOGST(DEBUG, kTag) << "Invalid stream index " << stream_index;
    status = ZX_ERR_INVALID_ARGS;
    return;
  }
  const auto& stream_config = config.stream_configs[stream_index];

  if (image_format_index >= stream_config.image_formats.size()) {
    FX_LOGST(DEBUG, kTag) << "Invalid image format index " << image_format_index;
    status = ZX_ERR_INVALID_ARGS;
    return;
  }

  if (buffer_collection.buffer_count == 0) {
    FX_LOGST(DEBUG, kTag) << "Invalid buffer count " << buffer_collection.buffer_count;
    status = ZX_ERR_INVALID_ARGS;
    return;
  }

  // Get Internal Configuration
  InternalConfigInfo* internal_config;
  status = GetInternalConfiguration(config_index, &internal_config);
  if (status != ZX_OK) {
    FX_PLOGST(DEBUG, kTag, status) << "Unable to get Internal configuration";
    return;
  }

  // Check the Input Stream Type and see if it is already created
  auto* stream_config_node =
      GetStreamConfigNode(internal_config, stream_config.properties.stream_type());
  if (stream_config_node == nullptr) {
    FX_LOGST(DEBUG, kTag) << "Unable to get Internal stream config node";
    return;
  }

  StreamCreationData info;
  info.output_buffers = std::move(buffer_collection);
  info.image_format_index = image_format_index;
  info.node = *stream_config_node;
  info.stream_config = fidl::Clone(stream_config);
  info.frame_rate_range = internal_config->frame_rate_range;

  cleanup.cancel();

  // We now have the stream_config_node which needs to be configured
  // Configure the stream pipeline
  pipeline_manager_.ConfigureStreamPipeline(std::move(info), std::move(stream));
}

void ControllerImpl::EnableStreaming() { pipeline_manager_.StartStreaming(); }

void ControllerImpl::DisableStreaming() { pipeline_manager_.StopStreaming(); }

void ControllerImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  fuchsia::camera2::DeviceInfo camera_device_info;
  camera_device_info.set_vendor_name(kCameraVendorName);
  camera_device_info.set_vendor_id(kCameraVendorId);
  camera_device_info.set_product_name(kCameraProductName);
  camera_device_info.set_product_id(kCameraProductId);
  camera_device_info.set_type(fuchsia::camera2::DeviceType::BUILTIN);
  callback(std::move(camera_device_info));
}

}  // namespace camera
