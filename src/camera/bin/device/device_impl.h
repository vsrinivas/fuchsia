// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_BIN_DEVICE_DEVICE_IMPL_H_
#define SRC_CAMERA_BIN_DEVICE_DEVICE_IMPL_H_

#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/camera3/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fit/result.h>
#include <zircon/status.h>

#include <map>
#include <memory>
#include <vector>

#include "src/camera/bin/device/stream_impl.h"

class DeviceImpl {
 public:
  DeviceImpl();
  ~DeviceImpl();
  static fit::result<std::unique_ptr<DeviceImpl>, zx_status_t> Create(
      fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> controller);
  fidl::InterfaceRequestHandler<fuchsia::camera3::Device> GetHandler();

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request);
  void OnControllerDisconnected(zx_status_t status);

  // Posts a task to remove the client with the given id.
  void PostRemoveClient(uint64_t id);

  // Posts a task to update the current configuration.
  void PostSetConfiguration(uint32_t index);

  // Sets the current configuration to the provided index.
  void SetConfiguration(uint32_t index);

  class Client : public fuchsia::camera3::Device {
   public:
    Client(DeviceImpl& device);
    ~Client();
    static fit::result<std::unique_ptr<Client>, zx_status_t> Create(
        DeviceImpl& device, uint64_t id, fidl::InterfaceRequest<fuchsia::camera3::Device> request);

   private:
    void CloseConnection(zx_status_t status);
    void OnClientDisconnected(zx_status_t status);

    // |fuchsia::camera3::Device|
    void GetIdentifier(GetIdentifierCallback callback) override;
    void GetConfigurations(GetConfigurationsCallback callback) override;
    void WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) override;
    void SetCurrentConfiguration(uint32_t index) override;
    void WatchMuteState(WatchMuteStateCallback callback) override;
    void SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) override;
    void ConnectToStream(uint32_t index,
                         fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                         fidl::InterfaceRequest<fuchsia::camera3::Stream> request) override;
    void Rebind(fidl::InterfaceRequest<fuchsia::camera3::Device> request) override;

    DeviceImpl& device_;
    uint64_t id_;
    async::Loop loop_;
    fidl::Binding<fuchsia::camera3::Device> binding_;
  };

  async::Loop loop_;
  fuchsia::camera2::hal::ControllerPtr controller_;
  fuchsia::camera2::DeviceInfo device_info_;
  std::vector<fuchsia::camera2::hal::Config> configs_;
  std::vector<fuchsia::camera3::Configuration> configurations_;
  std::map<uint64_t, std::unique_ptr<Client>> clients_;
  uint64_t client_id_next_ = 1;

  uint32_t current_configuration_index_ = 0;
  std::vector<std::unique_ptr<StreamImpl>> streams_;

  friend class Client;
};

#endif  // SRC_CAMERA_BIN_DEVICE_DEVICE_IMPL_H_
