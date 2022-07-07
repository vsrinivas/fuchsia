// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/syslog/cpp/macros.h>

#include <iomanip>
#include <sstream>

#include "src/camera/bin/usb_device/device_impl.h"
#include "src/camera/bin/usb_device/uvc_hack.h"

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

  std::ostringstream oss;
  oss << std::hex << std::uppercase << std::setfill('0');

  // Fake vendor ID.
  // TODO(ernesthua) - Need to construct this from the device information fetched.
  oss << "0000:0000";

  callback(oss.str());
}

void DeviceImpl::Client::GetConfigurations(GetConfigurationsCallback callback) {
  FX_LOGS(INFO) << log_prefix_ << "called GetConfigurations()";

  // Fake configuration.
  // TODO(ernesthua) - Need to construct this from the device configurations fetched.
  std::vector<fuchsia::camera3::Configuration> configurations;
  {
    std::vector<fuchsia::camera3::StreamProperties> streams;
    {
      fuchsia::camera3::StreamProperties stream_properties;
      UvcHackGetClientStreamProperties(&stream_properties);
      streams.push_back(std::move(stream_properties));
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
  FX_LOGS(ERROR) << log_prefix_ << "FIDL call not supported";
}

void DeviceImpl::Client::SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) {
  FX_LOGS(ERROR) << log_prefix_ << "FIDL call not supported";
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
