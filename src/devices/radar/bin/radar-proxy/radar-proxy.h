// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_RADAR_BIN_RADAR_PROXY_RADAR_PROXY_H_
#define SRC_DEVICES_RADAR_BIN_RADAR_PROXY_RADAR_PROXY_H_

#include <fuchsia/hardware/radar/cpp/fidl.h>
#include <lib/async/task.h>
#include <lib/zx/status.h>

#include <memory>

namespace radar {

class RadarDeviceConnector {
 public:
  // Synchronously connects to the given radar device and returns the client end. Calling threads
  // must have a default dispatcher.
  virtual fuchsia::hardware::radar::RadarBurstReaderProviderPtr ConnectToRadarDevice(
      int dir_fd, const std::string& filename) = 0;

  // Calls ConnectToRadarDevice() on all available devices, and returns the first one that is able
  // to connect successfully. Calling threads must have a default dispatcher.
  virtual fuchsia::hardware::radar::RadarBurstReaderProviderPtr ConnectToFirstRadarDevice() = 0;
};

class RadarProxy : public fuchsia::hardware::radar::RadarBurstReaderProvider {
 public:
  static constexpr char kRadarDeviceDirectory[] = "/dev/class/radar";

  explicit RadarProxy(RadarDeviceConnector* connector = nullptr);

  void Connect(fidl::InterfaceRequest<fuchsia::hardware::radar::RadarBurstReader> server,
               ConnectCallback callback) override;

  // Called by a DeviceWatcher when /dev/class/radar has a new device.
  void DeviceAdded(int dir_fd, const std::string& filename);

 private:
  class DefaultRadarDeviceConnector : public RadarDeviceConnector {
   public:
    fuchsia::hardware::radar::RadarBurstReaderProviderPtr ConnectToRadarDevice(
        int dir_fd, const std::string& filename) override;
    fuchsia::hardware::radar::RadarBurstReaderProviderPtr ConnectToFirstRadarDevice() override;
  };

  void ErrorHandler(zx_status_t status);

  RadarDeviceConnector* connector_;
  fuchsia::hardware::radar::RadarBurstReaderProviderPtr radar_client_;
  DefaultRadarDeviceConnector default_connector_;
  bool new_devices_ = false;
};

}  // namespace radar

#endif  // SRC_DEVICES_RADAR_BIN_RADAR_PROXY_RADAR_PROXY_H_
