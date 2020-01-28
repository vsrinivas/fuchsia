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

using ClientId = uint64_t;
using TransientDeviceId = uint64_t;
using PersistentDeviceId = uint64_t;

struct UniqueDevice {
  TransientDeviceId id;
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller;
};

using DevicesMap = std::unordered_map<PersistentDeviceId, UniqueDevice>;

class DeviceWatcherImpl {
 public:
  DeviceWatcherImpl();
  ~DeviceWatcherImpl();
  static fit::result<std::unique_ptr<DeviceWatcherImpl>, zx_status_t> Create();
  fit::result<PersistentDeviceId, zx_status_t> AddDevice(
      fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller);
  void UpdateClients();
  fidl::InterfaceRequestHandler<fuchsia::camera3::DeviceWatcher> GetHandler();

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request);

  // Implements the server endpoint for a single client, and maintains per-client state.
  class Client : public fuchsia::camera3::DeviceWatcher {
   public:
    Client();
    static fit::result<std::unique_ptr<Client>, zx_status_t> Create(
        ClientId id, fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request,
        async_dispatcher_t* dispatcher);
    void UpdateDevices(const DevicesMap& devices);
    operator bool();

   private:
    void CheckDevicesChanged();
    // |fuchsia::camera3::DeviceWatcher|
    void WatchDevices(WatchDevicesCallback callback) override;
    void ConnectToDevice(TransientDeviceId id,
                         fidl::InterfaceRequest<fuchsia::camera3::Device> request) override;

    ClientId id_;
    fidl::Binding<fuchsia::camera3::DeviceWatcher> binding_;
    WatchDevicesCallback callback_;
    std::set<TransientDeviceId> last_known_ids_;
    std::optional<std::set<TransientDeviceId>> last_sent_ids_;
  };

  async::Loop loop_;
  TransientDeviceId device_id_next_ = 1;
  DevicesMap devices_;
  ClientId client_id_next_ = 1;
  std::unordered_map<ClientId, std::unique_ptr<Client>> clients_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_
