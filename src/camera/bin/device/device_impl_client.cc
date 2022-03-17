// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <sstream>

#include "src/camera/bin/device/device_impl.h"

namespace camera {

DeviceImpl::Client::Client(DeviceImpl& device, uint64_t id,
                           fidl::InterfaceRequest<fuchsia::camera3::Device> request)
    : device_(device), id_(id), binding_(this, std::move(request)) {
  log_prefix_ = "device client (koid = " + std::to_string(GetRelatedKoid(binding_)) + "): ";
  FX_LOGS(INFO) << log_prefix_ << "new device client, id = " << id_;
  binding_.set_error_handler(fit::bind_member(this, &DeviceImpl::Client::OnClientDisconnected));
}

DeviceImpl::Client::~Client() = default;

void DeviceImpl::Client::ConfigurationUpdated(uint32_t index) { configuration_.Set(index); }

void DeviceImpl::Client::MuteUpdated(MuteState mute_state) { mute_state_.Set(mute_state); }

void DeviceImpl::Client::OnClientDisconnected(zx_status_t status) {
  FX_PLOGS(INFO, status) << log_prefix_ << "closed connection";
  device_.RemoveClient(id_);
}

void DeviceImpl::Client::CloseConnection(zx_status_t status) {
  binding_.Close(status);
  device_.RemoveClient(id_);
}

void DeviceImpl::Client::GetIdentifier(GetIdentifierCallback callback) {
  FX_LOGS(INFO) << log_prefix_ << "called GetIdentifier()";
  if (!device_.device_info_.has_vendor_id() || !device_.device_info_.has_product_id()) {
    callback(cpp17::nullopt);
    return;
  }

  std::ostringstream oss;
  oss << std::hex << std::uppercase << std::setfill('0');
  oss << std::setw(4) << device_.device_info_.vendor_id();
  oss << std::setw(4) << device_.device_info_.product_id();
  callback(oss.str());
}

void DeviceImpl::Client::GetConfigurations(GetConfigurationsCallback callback) {
  FX_LOGS(INFO) << log_prefix_ << "called GetConfigurations()";
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
  FX_LOGS(INFO) << log_prefix_ << "called GetConfigurations2()";
  callback(fidl::Clone(device_.configurations_));
}

void DeviceImpl::Client::WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) {
  FX_LOGS(INFO) << log_prefix_ << "called WatchCurrentConfiguration()";
  if (configuration_.Get(std::move(callback))) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void DeviceImpl::Client::SetCurrentConfiguration(uint32_t index) {
  FX_LOGS(INFO) << log_prefix_ << "called SetCurrentConfiguration(" << index << ")";
  if (index < 0 || index >= device_.configurations_.size()) {
    CloseConnection(ZX_ERR_OUT_OF_RANGE);
    return;
  }

  device_.SetConfiguration(index);
}

void DeviceImpl::Client::WatchMuteState(WatchMuteStateCallback callback) {
  FX_LOGS(INFO) << log_prefix_ << "called WatchMuteState()";
  if (mute_state_.Get([callback = std::move(callback)](MuteState mute_state) {
        callback(mute_state.software_muted, mute_state.hardware_muted);
      })) {
    CloseConnection(ZX_ERR_BAD_STATE);
  }
}

void DeviceImpl::Client::SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) {
  FX_LOGS(INFO) << log_prefix_ << "called SetSoftwareMuteState(" << muted << ")";
  if (!muted) {
    callback();
    callback = [] {};
  }
  device_.SetSoftwareMuteState(muted, std::move(callback));
}

void DeviceImpl::Client::ConnectToStream(uint32_t index,
                                         fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  FX_LOGS(INFO) << log_prefix_ << "called ConnectToStream(" << index << ")";
  device_.ConnectToStream(index, std::move(request));
}

void DeviceImpl::Client::Rebind(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  FX_LOGS(INFO) << log_prefix_ << "called Rebind(koid = " << GetRelatedKoid(request) << ")";
  device_.Bind(std::move(request));
}

}  // namespace camera
