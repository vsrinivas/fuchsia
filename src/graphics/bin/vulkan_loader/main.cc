// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/vulkan/loader/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

// Implements the vulkan loader's Loader service which provides the client
// driver portion to the loader as a VMO.
class LoaderImpl final : public fuchsia::vulkan::loader::Loader {
 public:
  LoaderImpl() = default;
  ~LoaderImpl() final = default;

  // Adds a binding for fuchsia::vulkan::loader::Loader to |outgoing|
  void Add(const std::shared_ptr<sys::OutgoingDirectory>& outgoing) {
    outgoing->AddPublicService(fidl::InterfaceRequestHandler<fuchsia::vulkan::loader::Loader>(
        [this](fidl::InterfaceRequest<fuchsia::vulkan::loader::Loader> request) {
          bindings_.AddBinding(this, std::move(request), nullptr);
        }));
  }

 private:
  // fuchsia::vulkan::loader::Loader impl
  void Get(std::string name, GetCallback callback) {
    // TODO(fxbug.dev/13078): Load this from a package's data directory, not /system/lib
    std::string load_path = "/system/lib/" + name;
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

  fidl::BindingSet<fuchsia::vulkan::loader::Loader> bindings_;
};

int main(int argc, const char* const* argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fxl::SetLogSettingsFromCommandLine(fxl::CommandLineFromArgcArgv(argc, argv));

  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  LoaderImpl loader_impl;
  loader_impl.Add(context->outgoing());

  FX_LOGS(INFO) << "Vulkan loader initialized.";
  loop.Run();
  return 0;
}
