// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/gpu/magma/cpp/fidl_test_base.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/process/lifecycle/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/processargs.h>

#include "sdk/lib/fidl/cpp/binding_set.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

class FakeMagmaDevice : public fuchsia::gpu::magma::testing::Device_TestBase {
 public:
  void NotImplemented_(const std::string& name) override {
    FX_LOGS(ERROR) << "Magma doing notimplemented with " << name;
  }

  void GetIcdList(GetIcdListCallback callback) override {
    fuchsia::gpu::magma::IcdInfo info;
    info.set_component_url(
        "fuchsia-pkg://fuchsia.com/vulkan_loader_tests#meta/test_vulkan_driver.cm");
    info.set_flags(fuchsia::gpu::magma::IcdFlags::SUPPORTS_VULKAN);
    std::vector<fuchsia::gpu::magma::IcdInfo> vec;
    vec.push_back(std::move(info));
    callback(std::move(vec));
  }

  void Query2(uint64_t id, Query2Callback callback) override {
    fuchsia::gpu::magma::Device_Query2_Result result;
    fuchsia::gpu::magma::Device_Query2_Response response;
    response.result = 5;
    result.set_response(response);
    callback(std::move(result));
  }

  fidl::InterfaceRequestHandler<fuchsia::gpu::magma::Device> GetHandler() {
    return bindings_.GetHandler(this);
  }

  void CloseAll() { bindings_.CloseAll(); }

 private:
  fidl::BindingSet<fuchsia::gpu::magma::Device> bindings_;
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

// Serve /pkg as the outgoing directory.
int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  LifecycleHandler handler(&loop);
  fxl::SetLogSettingsFromCommandLine(fxl::CommandLineFromArgcArgv(argc, argv));
  fidl::InterfaceHandle<fuchsia::io::Directory> pkg_dir;
  zx_status_t status;
  status = fdio_open("/pkg", fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE,
                     pkg_dir.NewRequest().TakeChannel().release());
  if (status != ZX_OK) {
    FX_PLOGST(FATAL, nullptr, status) << "Failed to open package";
    return -1;
  }

  // Use fs:: instead of vfs:: because vfs doesn't support executable directories.
  fs::SynchronousVfs vfs(loop.dispatcher());
  auto root = fbl::MakeRefCounted<fs::PseudoDir>();

  // Add a dev directory that the loader can watch for devices to be added.
  auto dev_gpu_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  root->AddEntry("dev-gpu", dev_gpu_dir);
  FakeMagmaDevice magma_device;
  dev_gpu_dir->AddEntry(
      "000", fbl::MakeRefCounted<fs::Service>([&magma_device](zx::channel channel) {
        magma_device.GetHandler()(
            fidl::InterfaceRequest<fuchsia::gpu::magma::Device>(std::move(channel)));
        return ZX_OK;
      }));

  auto dev_goldfish_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  root->AddEntry("dev-goldfish-pipe", dev_goldfish_dir);

  auto dev_dir = fbl::MakeRefCounted<fs::PseudoDir>();
  root->AddEntry("dev", dev_gpu_dir);

  zx::channel dir_request = zx::channel(zx_take_startup_handle(PA_DIRECTORY_REQUEST));
  auto options = fs::VnodeConnectionOptions::ReadExec();
  options.rights.write = 1;
  status = vfs.Serve(root, std::move(dir_request), options);

  if (status != ZX_OK) {
    FX_PLOGST(FATAL, nullptr, status) << "Failed to serve outgoing.";
    return -1;
  }

  loop.Run();
  return 0;
}
