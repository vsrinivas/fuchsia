// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/opencl/loader/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

// This is a fake opencl loader service that implements just enough for the libopencl.so to work.
class LoaderImpl final : public fuchsia::opencl::loader::Loader {
 public:
  explicit LoaderImpl() {}
  ~LoaderImpl() final = default;

  // Adds a binding for fuchsia::opencl::loader::Loader to |outgoing|
  void Add(const std::shared_ptr<sys::OutgoingDirectory>& outgoing) {
    outgoing->AddPublicService(fidl::InterfaceRequestHandler<fuchsia::opencl::loader::Loader>(
        [this](fidl::InterfaceRequest<fuchsia::opencl::loader::Loader> request) {
          bindings_.AddBinding(this, std::move(request), nullptr);
        }));
  }

 private:
  // fuchsia::opencl::loader::Loader impl
  void Get(std::string name, GetCallback callback) override {
    // libopencl_fake.so is located inside this package.
    std::string load_path = "/pkg/lib/" + name;
    int fd;
    zx_status_t status =
        fdio_open_fd(load_path.c_str(),
                     fuchsia::io::OPEN_RIGHT_READABLE | fuchsia::io::OPEN_RIGHT_EXECUTABLE, &fd);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Could not open path " << load_path << ":" << status;
      callback({});
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

  void ConnectToDeviceFs(zx::channel channel) override {
    // The fake libopencl implementation expects to be able to read
    // libopencl_fake.json from the device fs.
    fdio_open("/pkg/data/manifest", fuchsia::io::OPEN_RIGHT_READABLE, channel.release());
  }
  void GetSupportedFeatures(GetSupportedFeaturesCallback callback) override {
    fuchsia::opencl::loader::Features features =
        fuchsia::opencl::loader::Features::CONNECT_TO_DEVICE_FS |
        fuchsia::opencl::loader::Features::GET |
        fuchsia::opencl::loader::Features::CONNECT_TO_MANIFEST_FS;

    callback(features);
  }

  void ConnectToManifestFs(fuchsia::opencl::loader::ConnectToManifestOptions options,
                           zx::channel channel) override {
    fdio_open("/pkg/data/manifest", fuchsia::io::OPEN_RIGHT_READABLE, channel.release());
  }

  fidl::BindingSet<fuchsia::opencl::loader::Loader> bindings_;
};

int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  fxl::SetLogSettingsFromCommandLine(command_line);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  LoaderImpl loader_impl;
  loader_impl.Add(context->outgoing());
  loop.Run();
  return 0;
}
