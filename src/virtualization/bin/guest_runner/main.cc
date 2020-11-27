// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/vmm/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>

#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/synchronous_vfs.h>

namespace {

class RunnerImpl : public fuchsia::sys::Runner {
 public:
  explicit RunnerImpl(async_dispatcher_t* dispatcher) : vfs_(dispatcher) {
    context_->svc()->Connect(launcher_.NewRequest());
    context_->outgoing()->AddPublicService(bindings_.GetHandler(this));
  }

 private:
  // |fuchsia::sys::Runner|
  void StartComponent(
      fuchsia::sys::Package application, fuchsia::sys::StartupInfo startup_info,
      fidl::InterfaceRequest<fuchsia::sys::ComponentController> controller) override {
    fuchsia::sys::LaunchInfo launch_info;

    // Create a bridge between directory_request that we got and VMM's
    // directory_request.
    fidl::InterfaceHandle<fuchsia::io::Directory> outgoing_dir;
    launch_info.directory_request = outgoing_dir.NewRequest().TakeChannel();
    fbl::RefPtr<fs::PseudoDir> dir = fbl::AdoptRef(new fs::PseudoDir());
    dir->AddEntry("svc", fbl::AdoptRef(new fs::RemoteDir(outgoing_dir.TakeChannel())));
    vfs_.ServeDirectory(std::move(dir), std::move(startup_info.launch_info.directory_request));

    // This must list every service the VMM depends on. We don't provide any
    // implementations here since host_directory takes care of that for us.
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.emplace_back(fuchsia::virtualization::vmm::LaunchInfoProvider::Name_);

    // Pass-through some arguments directly to the VMM package.
    launch_info.url = "fuchsia-pkg://fuchsia.com/vmm#meta/vmm.cmx";
    launch_info.arguments = std::move(startup_info.launch_info.arguments);
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    for (size_t i = 0; i < startup_info.flat_namespace.paths.size(); ++i) {
      auto& path = startup_info.flat_namespace.paths[i];
      auto& directory = startup_info.flat_namespace.directories[i];
      // Clone the /svc directory handle for host_directory.
      if (path == "/svc") {
        service_list->host_directory.reset(fdio_service_clone(directory.get()));
      }
      // Expose the specific guest package under the /guest namespace.
      launch_info.flat_namespace->paths.emplace_back(path == "/pkg" ? "/guest" : path);
      launch_info.flat_namespace->directories.emplace_back(std::move(directory));
    }
    launch_info.additional_services = std::move(service_list);

    launcher_->CreateComponent(std::move(launch_info), std::move(controller));
  }

  std::unique_ptr<sys::ComponentContext> context_ =
      sys::ComponentContext::CreateAndServeOutgoingDirectory();
  fuchsia::sys::LauncherPtr launcher_;
  fidl::BindingSet<fuchsia::sys::Runner> bindings_;
  fs::SynchronousVfs vfs_;
};

}  // namespace

int main(int argc, char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  RunnerImpl runner(loop.dispatcher());
  return loop.Run();
}
