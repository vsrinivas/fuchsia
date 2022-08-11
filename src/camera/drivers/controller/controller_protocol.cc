// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller_protocol.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/fit/defer.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

#include "src/camera/drivers/controller/configs/product_config.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

ControllerImpl::ControllerImpl(async_dispatcher_t* dispatcher,
                               const ddk::SysmemProtocolClient& sysmem,
                               const ddk::IspProtocolClient& isp, const ddk::GdcProtocolClient& gdc,
                               const ddk::Ge2dProtocolClient& ge2d,
                               LoadFirmwareCallback load_firmware)
    : dispatcher_(dispatcher),
      binding_(this),
      pipeline_manager_(dispatcher, sysmem, isp, gdc, ge2d, std::move(load_firmware)),
      product_config_(ProductConfig::Create()) {
  binding_.set_error_handler(
      [](zx_status_t status) { FX_PLOGS(INFO, status) << "controller client disconnected"; });
  configs_ = product_config_->ExternalConfigs();
  internal_configs_ = product_config_->InternalConfigs();
}

void ControllerImpl::Connect(fidl::InterfaceRequest<fuchsia::camera2::hal::Controller> request) {
  if (binding_.is_bound()) {
    request.Close(ZX_ERR_ALREADY_BOUND);
    return;
  }
  binding_.Bind(std::move(request), dispatcher_);
}

void ControllerImpl::GetNextConfig(GetNextConfigCallback callback) {
  fuchsia::camera2::hal::Config config;

  if (config_count_ >= configs_.size()) {
    callback(nullptr, ZX_ERR_STOP);
    return;
  }
  config = fidl::Clone(configs_.at(config_count_));
  callback(std::make_unique<fuchsia::camera2::hal::Config>(std::move(config)), ZX_OK);
  config_count_++;
}

void ControllerImpl::CreateStream(uint32_t config_index, uint32_t stream_index,
                                  uint32_t image_format_index,
                                  fidl::InterfaceRequest<fuchsia::camera2::Stream> stream) {
  TRACE_DURATION("camera", "ControllerImpl::CreateStream");

  FX_LOGS(INFO) << "new request from remote channel koid "
                << fsl::GetRelatedKoid(stream.channel().get()) << " for c" << config_index << "s"
                << stream_index << "f" << image_format_index;

  if (config_index >= configs_.size()) {
    stream.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  const auto& config = configs_[config_index];

  if (stream_index >= config.stream_configs.size()) {
    stream.Close(ZX_ERR_INVALID_ARGS);
    return;
  }
  const auto& stream_config = config.stream_configs[stream_index];

  if (image_format_index >= stream_config.image_formats.size()) {
    stream.Close(ZX_ERR_INVALID_ARGS);
    return;
  }

  // The presence of the requests queue indicates the pipeline manager is in transition.
  if (requests_) {
    // If a new config request arrives before the previous shutdown completed, discard any pending
    // requests received in the interim period and just start tracking requests for the new config.
    if (config_index != pipeline_config_index_) {
      pipeline_config_index_ = config_index;
      requests_ = RequestQueue{};
    }
    requests_->emplace(stream_index, image_format_index, std::move(stream));
    return;
  }

  // TODO(fxbug.dev/100525): Move config index management into the pipeline manager, then delete
  // shutdown/queueing in this component.
  //
  // If the requested config is different from the current
  // config, handling it requires shutting down the current pipeline first.
  if (config_index != pipeline_config_index_) {
    pipeline_config_index_ = config_index;
    requests_ = RequestQueue{};
    requests_->emplace(stream_index, image_format_index, std::move(stream));
    pipeline_manager_.Shutdown([this]() mutable {
      TRACE_DURATION("camera", "ControllerImpl::CreateStream.shutdown.callback");
      pipeline_manager_.SetRoots(
          internal_configs_.configs_info[pipeline_config_index_].streams_info);
      auto requests = std::move(*requests_);
      requests_.reset();
      while (!requests.empty()) {
        auto [stream_index, image_format_index, stream] = std::move(requests.front());
        requests.pop();
        CreateStream(pipeline_config_index_, stream_index, image_format_index, std::move(stream));
      }
    });
    return;
  }

  auto& internal_config = internal_configs_.configs_info[config_index];

  StreamCreationData info{.roots = internal_config.streams_info};
  info.image_format_index = image_format_index;
  info.stream_config = fidl::Clone(stream_config);
  info.frame_rate_range = internal_config.frame_rate_range;

  // We now have the stream_config_node which needs to be configured
  // Configure the stream pipeline
  pipeline_manager_.ConfigureStreamPipeline(std::move(info), std::move(stream));
}

void ControllerImpl::EnableStreaming() { pipeline_manager_.SetStreamingEnabled(true); }

void ControllerImpl::DisableStreaming() { pipeline_manager_.SetStreamingEnabled(false); }

void ControllerImpl::GetDeviceInfo(GetDeviceInfoCallback callback) {
  callback(ProductConfig::DeviceInfo());
}

}  // namespace camera
