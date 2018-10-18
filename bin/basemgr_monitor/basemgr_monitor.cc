// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/macros.h>

namespace modular {

class BasemgrMonitorApp : fuchsia::modular::BasemgrMonitor {
 public:
  BasemgrMonitorApp()
      : context_(component::StartupContext::CreateFromStartupInfoNotChecked()) {
    context_->outgoing().AddPublicService<fuchsia::modular::BasemgrMonitor>(
        [this](
            fidl::InterfaceRequest<fuchsia::modular::BasemgrMonitor> request) {
          bindings_.AddBinding(this, std::move(request));
        });
  }

 private:
  // |fuchsia::modular::BasemgrMonitor|
  void GetConnectionCount(GetConnectionCountCallback callback) override {
    callback(bindings_.size());
  }

  std::unique_ptr<component::StartupContext> context_;
  fidl::BindingSet<fuchsia::modular::BasemgrMonitor> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrMonitorApp);
};

}  // namespace modular

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  modular::BasemgrMonitorApp app;
  loop.Run();
  return 0;
}
