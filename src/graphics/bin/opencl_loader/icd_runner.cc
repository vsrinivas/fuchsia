// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "icd_runner.h"

#include <fuchsia/component/runner/cpp/fidl.h>
#include <fuchsia/io/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/io.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/storage/vfs/cpp/pseudo_dir.h"
#include "src/lib/storage/vfs/cpp/pseudo_file.h"
#include "src/lib/storage/vfs/cpp/remote_dir.h"
#include "src/lib/storage/vfs/cpp/service.h"
#include "src/lib/storage/vfs/cpp/synchronous_vfs.h"
#include "src/lib/storage/vfs/cpp/vfs.h"
#include "src/lib/storage/vfs/cpp/vfs_types.h"

class ComponentControllerImpl : public fuchsia::component::runner::ComponentController {
 public:
  ComponentControllerImpl() : vfs_(async_get_default_dispatcher()) {}

  zx_status_t Initialize(fidl::InterfaceRequest<fuchsia::io::Directory> directory_request,
                         fidl::InterfaceHandle<fuchsia::io::Directory> pkg_directory) {
    auto root = fbl::MakeRefCounted<fs::PseudoDir>();
    auto remote = fbl::MakeRefCounted<fs::RemoteDir>(
        fidl::ClientEnd<fuchsia_io::Directory>(pkg_directory.TakeChannel()));
    root->AddEntry("pkg", remote);

    zx::channel dir_request = directory_request.TakeChannel();
    auto options = fs::VnodeConnectionOptions::ReadExec();
    options.rights.write = 1;
    return vfs_.Serve(root, std::move(dir_request), options);
  }

  void Add(std::unique_ptr<ComponentControllerImpl> controller,
           fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> request) {
    controller_.AddBinding(std::move(controller), std::move(request));
  }

 private:
  void Kill() override { controller_.CloseAll(); }
  void Stop() override { controller_.CloseAll(); }

  fs::SynchronousVfs vfs_;
  // This BindingSet should have at most one member.
  fidl::BindingSet<fuchsia::component::runner::ComponentController,
                   std::unique_ptr<ComponentControllerImpl>>
      controller_;
};

void IcdRunnerImpl::Add(const std::shared_ptr<sys::OutgoingDirectory>& outgoing,
                        async_dispatcher_t* dispatcher) {
  outgoing->AddPublicService(
      fidl::InterfaceRequestHandler<fuchsia::component::runner::ComponentRunner>(
          [this, dispatcher](
              fidl::InterfaceRequest<fuchsia::component::runner::ComponentRunner> request) {
            bindings_.AddBinding(this, std::move(request), dispatcher);
          }));
}

void IcdRunnerImpl::Start(
    fuchsia::component::runner::ComponentStartInfo start_info,
    fidl::InterfaceRequest<fuchsia::component::runner::ComponentController> controller) {
  fidl::InterfaceHandle<fuchsia::io::Directory> pkg_directory;
  for (auto& ns_entry : *start_info.mutable_ns()) {
    if (!ns_entry.has_path() || !ns_entry.has_directory()) {
      controller.Close(static_cast<zx_status_t>(fuchsia::component::Error::INVALID_ARGUMENTS));
      return;
    }
    if (ns_entry.path() != "/pkg") {
      continue;
    }
    pkg_directory = std::move(*ns_entry.mutable_directory());
    break;
  }
  if (!pkg_directory) {
    FX_LOGS(ERROR) << "No package directory found for " << start_info.resolved_url();
    controller.Close(static_cast<zx_status_t>(fuchsia::component::Error::INVALID_ARGUMENTS));
    return;
  }
  auto impl = std::make_unique<ComponentControllerImpl>();
  zx_status_t status =
      impl->Initialize(std::move(*start_info.mutable_outgoing_dir()), std::move(pkg_directory));
  if (status != ZX_OK) {
    controller.Close(status);
    return;
  }
  auto impl_ptr = impl.get();
  impl_ptr->Add(std::move(impl), std::move(controller));
}
