// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of the DeviceShell service that passes a dummy user
// name to its UserProvider.

#include <memory>

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr char kDummyUserName[] = "user1";

class DummyDeviceShellApp :
      public modular::SingleServiceViewApp<modular::DeviceShellFactory>,
      public modular::DeviceShell {
 public:
  DummyDeviceShellApp() : binding_(this) {}
  ~DummyDeviceShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |DeviceShellFactory|
  void Create(
      fidl::InterfaceHandle<modular::UserProvider> user_provider,
      fidl::InterfaceRequest<modular::DeviceShell> device_shell_request) override {
    user_provider_.Bind(std::move(user_provider));

    FTL_DCHECK(!binding_.is_bound());
    binding_.Bind(std::move(device_shell_request));

    Connect();
  }

  void Connect() {
    if (user_provider_ && view_owner_request_) {
      user_provider_->Login(kDummyUserName, std::move(view_owner_request_));
    }
  }

  fidl::Binding<DeviceShell> binding_;
  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  modular::UserProviderPtr user_provider_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  mtl::MessageLoop loop;
  DummyDeviceShellApp app;
  loop.Run();
  return 0;
}
