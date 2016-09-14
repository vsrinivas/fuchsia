// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User Shell.
// This takes |username| as a command line argument and passes it to Device
// Runner.

#include <mojo/system/main.h>

#include "apps/modular/mojom_hack/device_runner.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/strong_binding.h"

namespace {

class DummyDeviceShellImpl : public device_runner::DeviceShell {
 public:
  explicit DummyDeviceShellImpl(
      const std::string& username,
      mojo::InterfaceRequest<device_runner::DeviceShell> request)
      : binding_(this, std::move(request)), username_(username) {}
  ~DummyDeviceShellImpl() override{};

  void SetDeviceRunner(mojo::InterfaceHandle<device_runner::DeviceRunner>
                           device_runner) override {
    mojo::InterfacePtr<device_runner::DeviceRunner> service =
        mojo::InterfacePtr<device_runner::DeviceRunner>::Create(
            device_runner.Pass());
    service->Login(username_);
  }

 private:
  mojo::StrongBinding<device_runner::DeviceShell> binding_;
  std::string username_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellImpl);
};

class DummyDeviceShellApp : public mojo::ApplicationImplBase {
 public:
  DummyDeviceShellApp() {}
  ~DummyDeviceShellApp() override {}

 private:
  void OnInitialize() override {
    if (args().size() != 1) {
      FTL_DLOG(INFO) << "dummy-device-runner expects 1 additional argument.\n"
                     << "Usage: mojo:dummy-device-runner [user]";
      return;
    }

    FTL_LOG(INFO) << "dummy-device-device init";
    username_ = args()[0];
  }

  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    // Register |DummyDeviceShellService| implementation.
    service_provider_impl->AddService<device_runner::DeviceShell>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<device_runner::DeviceShell>
                   device_shell_request) {
          new DummyDeviceShellImpl(username_, std::move(device_shell_request));
        });
    return true;
  }

  std::string username_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellApp);
};

}  // namespace

MojoResult MojoMain(MojoHandle application_request) {
  DummyDeviceShellApp app;
  return mojo::RunApplication(application_request, &app);
}
