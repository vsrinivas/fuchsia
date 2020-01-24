// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_
#define SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>
#include <set>
#include <unordered_map>

struct UniqueDevice {
  uint64_t id;
  fuchsia::camera2::hal::ControllerPtr controller;
  std::optional<std::string> current_path;
};

using DevicesMap = std::unordered_map<uint64_t, UniqueDevice>;

class DeviceWatcherImpl {
 public:
  DeviceWatcherImpl();
  ~DeviceWatcherImpl();
  static fit::result<std::unique_ptr<DeviceWatcherImpl>, zx_status_t> Create();
  zx_status_t AddDevice(std::string path);
  zx_status_t RemoveDevice(std::string path);
  void UpdateClients();
  fidl::InterfaceRequestHandler<fuchsia::camera3::DeviceWatcher> GetHandler();

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request);

  // Implements the server endpoint for a single client, and maintains per-client state.
  class Client : public fuchsia::camera3::DeviceWatcher {
   public:
    Client();
    static fit::result<std::unique_ptr<Client>, zx_status_t> Create(
        uint64_t id, fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request,
        async_dispatcher_t* dispatcher);
    void UpdateDevices(const DevicesMap& devices);
    operator bool();

   private:
    void CheckDevicesChanged();
    // |fuchsia::camera3::DeviceWatcher|
    void WatchDevices(WatchDevicesCallback callback) override;
    void ConnectToDevice(uint64_t id,
                         fidl::InterfaceRequest<fuchsia::camera3::Device> request) override;

    uint64_t id_;
    fidl::Binding<fuchsia::camera3::DeviceWatcher> binding_;
    WatchDevicesCallback callback_;
    std::set<uint64_t> last_known_ids_;
    std::optional<std::set<uint64_t>> last_sent_ids_;
  };

  async::Loop loop_;
  uint64_t device_id_next_ = 1;
  std::mutex devices_lock_;
  DevicesMap devices_ __TA_GUARDED(devices_lock_);
  uint64_t client_id_next_ = 1;
  std::unordered_map<uint64_t, std::unique_ptr<Client>> clients_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_
