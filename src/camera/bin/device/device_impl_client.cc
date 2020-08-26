// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <sstream>

#include "src/camera/bin/device/device_impl.h"

DeviceImpl::Client::Client(DeviceImpl& device, uint64_t id,
                           fidl::InterfaceRequest<fuchsia::camera3::Device> request)
    : device_(device),
      id_(id),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      binding_(this, std::move(request), loop_.dispatcher()) {
  FX_LOGS(DEBUG) << "Device client " << id << " connected.";
  binding_.set_error_handler(fit::bind_member(this, &DeviceImpl::Client::OnClientDisconnected));
  std::ostringstream oss;
  oss << "Camera Device Client " << id;
  ZX_ASSERT(loop_.StartThread(oss.str().c_str()) == ZX_OK);
}

DeviceImpl::Client::~Client() { loop_.Shutdown(); }

void DeviceImpl::Client::PostConfigurationUpdated(uint32_t index) {
  async::PostTask(loop_.dispatcher(), [this, index]() { configuration_.Set(index); });
}

void DeviceImpl::Client::PostMuteUpdated(MuteState mute_state) {
  async::PostTask(loop_.dispatcher(), [this, mute_state] { mute_state_.Set(mute_state); });
}

void DeviceImpl::Client::OnClientDisconnected(zx_status_t status) {
  FX_PLOGS(DEBUG, status) << "Device client " << id_ << " disconnected.";
  device_.PostRemoveClient(id_);
}

void DeviceImpl::Client::CloseConnection(zx_status_t status) {
  binding_.Close(status);
  device_.PostRemoveClient(id_);
}

void DeviceImpl::Client::GetIdentifier(GetIdentifierCallback callback) {
  if (!device_.device_info_.has_vendor_id() || !device_.device_info_.has_product_id()) {
    callback(nullptr);
    return;
  }

  std::ostringstream oss;
  oss << std::hex << std::uppercase << std::setfill('0');
  oss << std::setw(4) << device_.device_info_.vendor_id();
  oss << std::setw(4) << device_.device_info_.product_id();
  callback(oss.str());
}

void DeviceImpl::Client::GetConfigurations(GetConfigurationsCallback callback) {
  std::vector<fuchsia::camera3::Configuration> configurations;
  for (const auto& configuration : device_.configurations_) {
    std::vector<fuchsia::camera3::StreamProperties> streams;
    for (const auto& stream : configuration.streams()) {
      streams.push_back(Convert(stream));
    }
    configurations.push_back({.streams = std::move(streams)});
  }
  callback(std::move(configurations));
}

void DeviceImpl::Client::GetConfigurations2(GetConfigurations2Callback callback) {
  callback(fidl::Clone(device_.configurations_));
}

void DeviceImpl::Client::WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) {
  if (configuration_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void DeviceImpl::Client::SetCurrentConfiguration(uint32_t index) {
  if (index < 0 || index >= device_.configurations_.size()) {
    CloseConnection(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  device_.PostSetConfiguration(index);
}

void DeviceImpl::Client::WatchMuteState(WatchMuteStateCallback callback) {
  if (mute_state_.Get([callback = std::move(callback)](MuteState mute_state) {
        callback(mute_state.software_muted, mute_state.hardware_muted);
      })) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void DeviceImpl::Client::SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) {
  if (!muted) {
    callback();
    callback = [] {};
  }
  fit::closure marshalling_callback = [this, callback = callback.share()]() mutable {
    async::PostTask(loop_.dispatcher(), [callback = std::move(callback)] { callback(); });
  };
  device_.PostSetSoftwareMuteState(muted, std::move(marshalling_callback));
}

void DeviceImpl::Client::ConnectToStream(uint32_t index,
                                         fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  device_.PostConnectToStream(index, std::move(request));
}

void DeviceImpl::Client::Rebind(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  device_.PostBind(std::move(request), false);
}
