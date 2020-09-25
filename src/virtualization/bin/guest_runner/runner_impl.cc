// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_runner/runner_impl.h"

#include <fuchsia/virtualization/vmm/cpp/fidl.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

#include <memory>

#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>

#include "lib/svc/cpp/service_provider_bridge.h"

namespace guest_runner {

RunnerImpl::RunnerImpl()
    : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()),
      vfs_(async_get_default_dispatcher()) {
  context_->svc()->Connect(launcher_.NewRequest());
  context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void RunnerImpl::StartComponent(
    fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
    ::fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) {
  fuchsia::sys::LaunchInfo launch_info;

  // Create a bridge between directory_request that we got and vmm's
  // directory_request.
  fbl::RefPtr<fs::PseudoDir> dir = fbl::AdoptRef(new fs::PseudoDir());
  fuchsia::io::DirectoryPtr public_dir;
  launch_info.directory_request = public_dir.NewRequest().TakeChannel();
  dir->AddEntry("svc", fbl::AdoptRef(new fs::RemoteDir(public_dir.Unbind().TakeChannel())));
  vfs_.ServeDirectory(std::move(dir), std::move(startup_info.launch_info.directory_request));

  // Pass-through some arguments directly to the vmm package.
  launch_info.url = "fuchsia-pkg://fuchsia.com/vmm#meta/vmm.cmx";
  launch_info.arguments = std::move(startup_info.launch_info.arguments);
  launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
  for (size_t i = 0; i < startup_info.flat_namespace.paths.size(); ++i) {
    const auto& path = startup_info.flat_namespace.paths[i];
    if (path == "/pkg") {
      // Expose the specific guest package under the /guest namespace.
      launch_info.flat_namespace->paths.push_back("/guest");
      launch_info.flat_namespace->directories.push_back(
          std::move(startup_info.flat_namespace.directories[i]));
    } else if (path == "/svc") {
      // Hack: We've provided some 'additional_services' to the vmm, but those
      // are loaded in the /svc in the provided flat_namespace here. Appmgr
      // doesn't allow overriding the /svc namespace of the vmm, instead it
      // initialized it to the set of services requested in the vmm.cmx.
      //
      // The solution here is to invert the dependency between guest_manager and
      // the guest_runner. Apps that call the guest_manager directly can just
      // embed the artifacts they need into their own package and don't need to
      // use a companion guest package. Then the runner can be used for the
      // standalone guest packages (ex: linux_guest/zircon_guest).
      //
      // Note: the leaking of the |ServiceProviderBridge| is intentional. We
      // could wrap the ComponentController in one that we retain here so we can
      // intercept the error event and cleanup, but since this is temporary we
      // can live with this.
      //
      // See: fxbug.dev/12543
      auto bridge = new component::ServiceProviderBridge;
      auto service_list = fuchsia::sys::ServiceList::New();
      // This must list every service the vmm depends on. We don't provide
      // any implementations here since the ServiceProviderBridge takes care
      // of that for us via the backing_dir, which is the above /svc directory.
      service_list->names.push_back(fuchsia::virtualization::vmm::LaunchInfoProvider::Name_);
      bridge->set_backing_dir(std::move(startup_info.flat_namespace.directories[i]));
      service_list->provider = bridge->AddBinding();
      launch_info.additional_services = std::move(service_list);
    }
  }

  launcher_->CreateComponent(std::move(launch_info), std::move(controller));
}

}  // namespace guest_runner
