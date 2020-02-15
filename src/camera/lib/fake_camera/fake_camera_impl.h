// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_LIB_FAKE_CAMERA_FAKE_CAMERA_IMPL_H_
#define SRC_CAMERA_LIB_FAKE_CAMERA_FAKE_CAMERA_IMPL_H_

#include <fuchsia/camera3/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>

#include "src/camera/lib/fake_camera/fake_camera.h"

namespace camera {

class FakeCameraImpl : public FakeCamera, public fuchsia::camera3::Device {
 public:
  FakeCameraImpl();
  ~FakeCameraImpl() override;
  static fit::result<std::unique_ptr<FakeCameraImpl>, zx_status_t> Create(
      std::string identifier, std::vector<FakeConfiguration> configurations);
  fidl::InterfaceRequestHandler<fuchsia::camera3::Device> GetHandler() override;
  void SetHardwareMuteState(bool muted) override;

 private:
  void OnNewRequest(fidl::InterfaceRequest<fuchsia::camera3::Device> request);
  void OnDestruction();
  template <class T>
  void SetDisconnectErrorHandler(fidl::InterfacePtr<T>& p);

  // |fuchsia::hardware::camera3::Device|
  void GetIdentifier(GetIdentifierCallback callback) override;
  void GetConfigurations(GetConfigurationsCallback callback) override;
  void WatchCurrentConfiguration(WatchCurrentConfigurationCallback callback) override;
  void SetCurrentConfiguration(uint32_t index) override;
  void WatchMuteState(WatchMuteStateCallback callback) override;
  void SetSoftwareMuteState(bool muted, SetSoftwareMuteStateCallback callback) override;
  void ConnectToStream(uint32_t index,
                       fidl::InterfaceHandle<fuchsia::sysmem::BufferCollectionToken> token,
                       fidl::InterfaceRequest<fuchsia::camera3::Stream> request) override;
  void Rebind(fidl::InterfaceRequest<Device> request) override;

  async::Loop loop_;
  fidl::BindingSet<fuchsia::camera3::Device> bindings_;
  std::string identifier_;
  std::vector<FakeConfiguration> configurations_;
  std::vector<fuchsia::camera3::Configuration> real_configurations_;
  uint32_t current_configuration_index_ = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_LIB_FAKE_CAMERA_FAKE_CAMERA_IMPL_H_
