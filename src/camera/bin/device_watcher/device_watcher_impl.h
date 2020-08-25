// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_
#define SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/function.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <memory>
#include <queue>
#include <set>
#include <unordered_map>

#include "src/camera/bin/device_watcher/device_instance.h"

using ClientId = uint64_t;
using TransientDeviceId = uint64_t;
using PersistentDeviceId = uint64_t;

struct UniqueDevice {
  TransientDeviceId id;
  std::unique_ptr<DeviceInstance> instance;
};

using DevicesMap = std::unordered_map<PersistentDeviceId, UniqueDevice>;

class DeviceWatcherImpl {
 public:
  DeviceWatcherImpl();
  ~DeviceWatcherImpl();
  static fit::result<std::unique_ptr<DeviceWatcherImpl>, zx_status_t> Create(
      fuchsia::sys::LauncherHandle launcher);
  fit::result<PersistentDeviceId, zx_status_t> AddDevice(
      fuchsia::hardware::camera::DeviceHandle camera);
  void UpdateClients();
  fidl::InterfaceRequestHandler<fuchsia::camera3::DeviceWatcher> GetHandler();

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request);

  // Implements the server endpoint for a single client, and maintains per-client state.
  class Client : public fuchsia::camera3::DeviceWatcher {
   public:
    explicit Client(DeviceWatcherImpl& watcher);
    static fit::result<std::unique_ptr<Client>, zx_status_t> Create(
        DeviceWatcherImpl& watcher, ClientId id,
        fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher> request,
        async_dispatcher_t* dispatcher);
    void UpdateDevices(const DevicesMap& devices);
    explicit operator bool();

   private:
    void CheckDevicesChanged();
    // |fuchsia::camera3::DeviceWatcher|
    void WatchDevices(WatchDevicesCallback callback) override;
    void ConnectToDevice(TransientDeviceId id,
                         fidl::InterfaceRequest<fuchsia::camera3::Device> request) override;

    DeviceWatcherImpl& watcher_;
    ClientId id_;
    fidl::Binding<fuchsia::camera3::DeviceWatcher> binding_;
    WatchDevicesCallback callback_;
    std::set<TransientDeviceId> last_known_ids_;
    std::optional<std::set<TransientDeviceId>> last_sent_ids_;
  };

  async::Loop loop_;
  fuchsia::sys::LauncherPtr launcher_;
  TransientDeviceId device_id_next_ = 1;
  DevicesMap devices_;
  ClientId client_id_next_ = 1;
  std::unordered_map<ClientId, std::unique_ptr<Client>> clients_;
  bool initial_update_received_ = false;
  std::queue<fidl::InterfaceRequest<fuchsia::camera3::DeviceWatcher>> requests_;
};

#endif  // SRC_CAMERA_BIN_DEVICE_WATCHER_DEVICE_WATCHER_IMPL_H_
