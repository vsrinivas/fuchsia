// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "radar-proxy.h"

#include <dirent.h>
#include <fcntl.h>
#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/fdio/directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/channel.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <fbl/unique_fd.h>

namespace radar {

using fuchsia::hardware::radar::RadarBurstReaderProviderPtr;

RadarProxy::RadarProxy(RadarDeviceConnector* connector)
    : connector_(connector == nullptr ? &default_connector_ : connector) {
  radar_client_.set_error_handler([&](zx_status_t status) { ErrorHandler(status); });
}

void RadarProxy::Connect(fidl::InterfaceRequest<fuchsia::hardware::radar::RadarBurstReader> server,
                         ConnectCallback callback) {
  if (!radar_client_.is_bound()) {
    fuchsia::hardware::radar::RadarBurstReaderProvider_Connect_Result result;
    result.set_err(fuchsia::hardware::radar::StatusCode::BIND_ERROR);
    callback(std::move(result));
    return;
  }

  radar_client_->Connect(
      std::move(server),
      [cb = std::move(callback)](
          fuchsia::hardware::radar::RadarBurstReaderProvider_Connect_Result result) {
        cb(std::move(result));
      });
}

void RadarProxy::DeviceAdded(int dir_fd, const std::string& filename) {
  if (radar_client_.is_bound()) {
    new_devices_ = true;
    return;
  }

  RadarBurstReaderProviderPtr radar_client = connector_->ConnectToRadarDevice(dir_fd, filename);
  if (radar_client.is_bound()) {
    radar_client_ = std::move(radar_client);
  }
}

RadarBurstReaderProviderPtr RadarProxy::DefaultRadarDeviceConnector::ConnectToRadarDevice(
    int dir_fd, const std::string& filename) {
  fbl::unique_fd device_fd(openat(dir_fd, filename.c_str(), O_RDWR));
  if (!device_fd) {
    return {};
  }

  zx::channel radar_channel;
  zx_status_t status =
      fdio_get_service_handle(device_fd.release(), radar_channel.reset_and_get_address());
  if (status == ZX_OK) {
    RadarBurstReaderProviderPtr radar_client;
    if (radar_client.Bind(std::move(radar_channel)) == ZX_OK) {
      return radar_client;
    }
  }

  return {};
}

RadarBurstReaderProviderPtr RadarProxy::DefaultRadarDeviceConnector::ConnectToFirstRadarDevice() {
  DIR* const devices_dir = opendir(kRadarDeviceDirectory);
  if (!devices_dir) {
    return {};
  }

  for (const dirent* device = readdir(devices_dir); device; device = readdir(devices_dir)) {
    RadarBurstReaderProviderPtr radar_client =
        ConnectToRadarDevice(dirfd(devices_dir), device->d_name);
    if (radar_client.is_bound()) {
      closedir(devices_dir);
      return radar_client;
    }
  }

  closedir(devices_dir);
  return {};
}

void RadarProxy::ErrorHandler(zx_status_t status) {
  FX_PLOGS(ERROR, status) << "Connection to radar device closed, attempting to reconnect";
  // Check for available devices now, just in case one was added before the connection closed. If
  // not, the DeviceWatcher will signal to connect when a new device becomes available.
  RadarBurstReaderProviderPtr radar_client = connector_->ConnectToFirstRadarDevice();
  if (radar_client.is_bound()) {
    radar_client_ = std::move(radar_client);
  }
}

}  // namespace radar
