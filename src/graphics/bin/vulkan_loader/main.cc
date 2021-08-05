// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/gpu/magma/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <fuchsia/sys2/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/processargs.h>

#include "src/graphics/bin/vulkan_loader/app.h"
#include "src/graphics/bin/vulkan_loader/icd_runner.h"
#include "src/graphics/bin/vulkan_loader/loader.h"
#include "src/graphics/bin/vulkan_loader/magma_dependency_injection.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

class LifecycleHandler : public fuchsia::process::lifecycle::Lifecycle {
 public:
  explicit LifecycleHandler(async::Loop* loop) : loop_(loop) {
    zx::channel channel = zx::channel(zx_take_startup_handle(PA_LIFECYCLE));
    bindings_.AddBinding(
        this, fidl::InterfaceRequest<fuchsia::process::lifecycle::Lifecycle>(std::move(channel)),
        loop_->dispatcher());
  }
  void Stop() override {
    loop_->Quit();
    bindings_.CloseAll();
  }

 private:
  async::Loop* loop_;

  fidl::BindingSet<fuchsia::process::lifecycle::Lifecycle> bindings_;
};

int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  async::Loop runner_loop(&kAsyncLoopConfigNoAttachToCurrentThread);

  LifecycleHandler lifecycle_handler(&loop);

  runner_loop.StartThread("IcdRunner");
  fxl::SetLogSettingsFromCommandLine(fxl::CommandLineFromArgcArgv(argc, argv));

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  LoaderApp app(context.get(), loop.dispatcher());
  zx_status_t status = app.InitDeviceWatcher();

  if (status != ZX_OK) {
    FX_LOGS(INFO) << "Failed to initialize device watcher " << status;
    return -1;
  }

  status = app.InitDeviceFs();

  if (status != ZX_OK) {
    FX_LOGS(INFO) << "Failed to initialize device fs " << status;
    return -1;
  }

  status = app.InitManifestFs();
  if (status != ZX_OK) {
    FX_LOGS(INFO) << "Failed to initialize manifest fs " << status;
    return -1;
  }

  MagmaDependencyInjection manager(context.get());
  status = manager.Initialize();
  if (status != ZX_OK) {
    FX_LOGS(INFO) << "Failed to initialize gpu manager " << status;
    return -1;
  }

  IcdRunnerImpl component_runner;
  component_runner.Add(context->outgoing(), runner_loop.dispatcher());

  LoaderImpl::Add(&app, context->outgoing());

  FX_LOGS(INFO) << "Vulkan loader initialized.";
  loop.Run();
  runner_loop.Shutdown();
  loop.Shutdown();
  return 0;
}
