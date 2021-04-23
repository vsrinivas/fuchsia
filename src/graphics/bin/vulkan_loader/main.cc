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
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

// Implements the vulkan loader's Loader service which provides the client
// driver portion to the loader as a VMO.
class LoaderImpl final : public fuchsia::vulkan::loader::Loader, public LoaderApp::Observer {
 public:
  explicit LoaderImpl(LoaderApp* app) : app_(app) {}
  ~LoaderImpl() final { app_->RemoveObserver(this); }

  // Adds a binding for fuchsia::vulkan::loader::Loader to |outgoing|
  void Add(const std::shared_ptr<sys::OutgoingDirectory>& outgoing) {
    outgoing->AddPublicService(fidl::InterfaceRequestHandler<fuchsia::vulkan::loader::Loader>(
        [this](fidl::InterfaceRequest<fuchsia::vulkan::loader::Loader> request) {
          bindings_.AddBinding(this, std::move(request), nullptr);
        }));
  }

  // LoaderApp::Observer implementation.
  void OnIcdListChanged(LoaderApp* app) override {
    for (auto it = callbacks_.begin(); it != callbacks_.end();) {
      std::optional<zx::vmo> vmo = app->GetMatchingIcd(it->first);
      if (!vmo) {
        ++it;
      } else {
        it->second(*std::move(vmo));
        it = callbacks_.erase(it);
      }
    }
    if (callbacks_.empty()) {
      app_->RemoveObserver(this);
    }
  }

 private:
  // fuchsia::vulkan::loader::Loader impl
  void Get(std::string name, GetCallback callback) override {
    // TODO(fxbug.dev/13078): Remove code to load from /system/lib.
    std::string load_path = "/system/lib/" + name;
    int fd;
    zx_status_t status =
        fdio_open_fd(load_path.c_str(),
                     fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE, &fd);
    if (status != ZX_OK) {
      AddCallback(std::move(name), std::move(callback));
      return;
    }
    zx::vmo vmo;
    status = fdio_get_vmo_exec(fd, vmo.reset_and_get_address());
    close(fd);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Could not clone vmo exec: " << status;
    }
    callback(std::move(vmo));
  }

  void AddCallback(std::string name, fit::function<void(zx::vmo)> callback) {
    std::optional<zx::vmo> vmo = app_->GetMatchingIcd(name);
    if (vmo) {
      callback(*std::move(vmo));
      return;
    }
    callbacks_.emplace_back(std::make_pair(std::move(name), std::move(callback)));
    if (callbacks_.size() == 1) {
      app_->AddObserver(this);
    }
  }

  LoaderApp* app_;

  fidl::BindingSet<fuchsia::vulkan::loader::Loader> bindings_;

  std::list<std::pair<std::string, fit::function<void(zx::vmo)>>> callbacks_;
};

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

  IcdRunnerImpl component_runner;
  component_runner.Add(context->outgoing(), runner_loop.dispatcher());

  LoaderImpl loader_impl(&app);
  loader_impl.Add(context->outgoing());

  FX_LOGS(INFO) << "Vulkan loader initialized.";
  loop.Run();
  runner_loop.Shutdown();
  loop.Shutdown();
  return 0;
}
