// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <sys/stat.h>
#include <sys/types.h>

// Implements the vulkan loader's Loader service which provides the client
// driver portion to the loader as a VMO.
class LoaderImpl : public fuchsia::vulkan::loader::Loader {
 public:
  LoaderImpl() = default;
  ~LoaderImpl() final = default;

  // Adds a binding for fuchsia::vulkan::loader::Loader to |outgoing|
  void Add(const std::shared_ptr<sys::OutgoingDirectory>& outgoing) {
    outgoing->AddPublicService(bindings_.GetHandler(this));
  }

 private:
  // fuchsia::vulkan::loader::Loader impl
  void Get(std::string name, GetCallback callback) {
    // TODO(MA-470): Load this from a package's data directory, not /system/lib
    std::string load_path = "/system/lib/" + name;
    int fd = open(load_path.c_str(), O_RDONLY);
    if (fd < 0) {
      FXL_LOG(ERROR) << "Could not open path " << load_path;
      callback({});
      return;
    }
    zx::vmo vmo;
    zx_status_t status = fdio_get_vmo_clone(fd, vmo.reset_and_get_address());
    close(fd);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not clone vmo: " << status;
    }
    status = vmo.replace_as_executable(zx::handle(), &vmo);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "Could not make vmo executable: " << status;
    }
    callback(std::move(vmo));
  }

  fidl::BindingSet<fuchsia::vulkan::loader::Loader> bindings_;
};

int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  fxl::SetLogSettingsFromCommandLine(fxl::CommandLineFromArgcArgv(argc, argv));

  auto context = sys::ComponentContext::Create();

  LoaderImpl loader_impl;
  loader_impl.Add(context->outgoing());

  loop.Run();
  return 0;
}
