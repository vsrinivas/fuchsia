// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/application_context.h"
#include "apps/modular/services/device/device_runner_monitor.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

namespace modular {

class DeviceRunnerMonitorApp : DeviceRunnerMonitor {
 public:
  DeviceRunnerMonitorApp()
      : app_context_(
            app::ApplicationContext::CreateFromStartupInfoNotChecked()) {
    app_context_->outgoing_services()->AddService<DeviceRunnerMonitor>(
        [this](fidl::InterfaceRequest<DeviceRunnerMonitor> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

 private:
  // |DeviceRunnerMonitor|
  void GetConnectionCount(const GetConnectionCountCallback& callback) override {
    callback(bindings_.size());
  }

  std::unique_ptr<app::ApplicationContext> app_context_;
  fidl::BindingSet<DeviceRunnerMonitor> bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DeviceRunnerMonitorApp);
};

}  // namespace modular

int main(int /*argc*/, const char** /*argv*/) {
  mtl::MessageLoop loop;
  modular::DeviceRunnerMonitorApp app;
  loop.Run();
  return 0;
}
