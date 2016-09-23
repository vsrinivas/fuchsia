// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User Shell.
// This takes |username| as a command line argument and passes it to Device
// Runner.

#include <mojo/system/main.h>

#include "apps/modular/device_runner/device_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace modular {

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::StrongBinding;

class DummyDeviceShellImpl : public DeviceShell {
 public:
  DummyDeviceShellImpl(const std::string& username,
                       InterfaceRequest<DeviceShell> request)
      : binding_(this, std::move(request)), username_(username) {}
  ~DummyDeviceShellImpl() override{};

  void SetDeviceRunner(InterfaceHandle<DeviceRunner> device_runner) override {
    device_runner_ = InterfacePtr<DeviceRunner>::Create(device_runner.Pass());
    device_runner_->Login(username_);
  }

 private:
  StrongBinding<DeviceShell> binding_;
  std::string username_;

  InterfacePtr<DeviceRunner> device_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellImpl);
};

class DummyDeviceShellApp : public ApplicationImplBase {
 public:
  DummyDeviceShellApp() {}
  ~DummyDeviceShellApp() override {}

 private:
  void OnInitialize() override {
    if (args().size() != 1) {
      FTL_DLOG(INFO) << "dummy-device-runner expects 1 additional argument.\n"
                     << "Usage: mojo:dummy_device_runner [user]";
      return;
    }

    FTL_LOG(INFO) << "dummy-device-device init";
    username_ = args()[0];
  }

  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    // Register |DummyDeviceShellService| implementation.
    service_provider_impl->AddService<DeviceShell>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<DeviceShell> device_shell_request) {
          new DummyDeviceShellImpl(username_, std::move(device_shell_request));
        });
    return true;
  }

  std::string username_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellApp);
};

}  // namespace modular

MojoResult MojoMain(MojoHandle application_request) {
  modular::DummyDeviceShellApp app;
  return mojo::RunApplication(application_request, &app);
}
