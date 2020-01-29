// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/default.h>
#include <lib/syslog/cpp/logger.h>

#include <sstream>

#include "src/camera/bin/device/device_impl.h"

DeviceImpl::Client::Client(DeviceImpl& device, uint64_t id,
                           fidl::InterfaceRequest<fuchsia::camera3::Device> request)
    : device_(device),
      id_(id),
      loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
      binding_(this, std::move(request), loop_.dispatcher()) {
  FX_LOGS(DEBUG) << "Device client " << id << " connected.";
  std::ostringstream oss;
  oss << "Camera Device Thread (Client ID = " << id << ")";
  ZX_ASSERT(loop_.StartThread(oss.str().c_str()) == ZX_OK);
}

DeviceImpl::Client::~Client() { loop_.Shutdown(); }

void DeviceImpl::Client::OnClientDisconnected(zx_status_t status) {
  FX_PLOGS(DEBUG, status) << "Device client " << id_ << " disconnected.";
  device_.PostRemoveClient(id_);
}

void DeviceImpl::Client::CloseConnection(zx_status_t status) {
  binding_.Close(status);
  device_.PostRemoveClient(id_);
}

void DeviceImpl::Client::GetIdentifier(GetIdentifierCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void DeviceImpl::Client::GetConfigurations(GetConfigurationsCallback callback) {
  callback(device_.configurations_);
}

void DeviceImpl::Client::WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void DeviceImpl::Client::SetCurrentConfiguration(uint32_t index) {
  if (index < 0 || index >= device_.configurations_.size()) {
    CloseConnection(ZX_ERR_OUT_OF_RANGE);
  }

  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void DeviceImpl::Client::WatchMuteState(WatchMuteStateCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void DeviceImpl::Client::SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) {
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}

void DeviceImpl::Client::ConnectToStream(
    uint32_t index, fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
    fidl::InterfaceRequest<fuchsia::camera3::Stream> request) {
  device_.PostConnectToStream(index, std::move(token), std::move(request));
}

void DeviceImpl::Client::Rebind(fidl::InterfaceRequest<fuchsia::camera3::Device> request) {
  request.Close(ZX_ERR_NOT_SUPPORTED);
  CloseConnection(ZX_ERR_NOT_SUPPORTED);
}
