// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy Device Shell. This passes a dummy user name
// to Device Runner.

#include <memory>

#include "apps/modular/lib/fidl/single_service_view_app.h"
#include "apps/modular/services/device/device_runner.fidl.h"
#include "apps/modular/services/device/device_shell.fidl.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {

constexpr char kDummyUserName[] = "user1";

class DummyDeviceShellApp :
      public modular::SingleServiceViewApp<modular::DeviceShell> {
 public:
  DummyDeviceShellApp() {}
  ~DummyDeviceShellApp() override = default;

 private:
  // |SingleServiceViewApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<modular::ServiceProvider> services) override {
    view_owner_request_ = std::move(view_owner_request);
    Connect();
  }

  // |DeviceShell|
  void SetDeviceRunner(
      fidl::InterfaceHandle<modular::DeviceRunner> device_runner) override {
    device_runner_.Bind(std::move(device_runner));
    Connect();
  }

  void Connect() {
    if (device_runner_ && view_owner_request_) {
      device_runner_->Login(kDummyUserName, std::move(view_owner_request_));
    }
  }

  fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request_;
  modular::DeviceRunnerPtr device_runner_;
  FTL_DISALLOW_COPY_AND_ASSIGN(DummyDeviceShellApp);
};

}  // namespace

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "dummy_device_shell main";
  mtl::MessageLoop loop;
  DummyDeviceShellApp app;
  loop.Run();
  return 0;
}
