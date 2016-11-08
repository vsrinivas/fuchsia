// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy Device Shell. This passes a dummy user name
// to Device Runner.

#include <memory>

#include "apps/modular/services/device/device_runner.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "apps/modular/mojo/single_service_view_app.h"
#include "apps/modular/mojo/strong_binding.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

constexpr char kDummyUserName[] = "user1";

using fidl::InterfaceHandle;
using fidl::InterfacePtr;
using fidl::InterfaceRequest;

class DummyDeviceShellImpl : public DeviceShell {
 public:
  DummyDeviceShellImpl(mozart::ViewManagerPtr view_manager,
                       InterfaceRequest<DeviceShell> device_shell_request,
                       InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : binding_(this, std::move(device_shell_request)),
        view_owner_request_(std::move(view_owner_request)) {}

  ~DummyDeviceShellImpl() override = default;

 private:
  void SetDeviceRunner(InterfaceHandle<DeviceRunner> device_runner) override {
    device_runner_.Bind(std::move(device_runner));
    device_runner_->Login(kDummyUserName, std::move(view_owner_request_));
  }

  StrongBinding<DeviceShell> binding_;
  InterfacePtr<DeviceRunner> device_runner_;
  InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellImpl);
};

}  // namespace

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "dummy_device_shell main";
  mtl::MessageLoop loop;
  modular::SingleServiceViewApp<modular::DeviceShell,
                                modular::DummyDeviceShellImpl>
      app;
  loop.Run();
  return 0;
}
