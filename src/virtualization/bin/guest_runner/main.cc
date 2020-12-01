// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/directory.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <fs/pseudo_dir.h>
#include <fs/remote_dir.h>
#include <fs/synchronous_vfs.h>

#include "src/virtualization/lib/guest_config/guest_config.h"

namespace {

zx_status_t ReadGuestCfg(const fuchsia::io::DirectoryHandle& dir, const std::string& path,
                         fuchsia::virtualization::GuestConfig* cfg) {
  auto open_at = [&dir](const std::string& path, fidl::InterfaceRequest<fuchsia::io::File> file) {
    return fdio_open_at(dir.channel().get(), path.data(), fuchsia::io::OPEN_RIGHT_READABLE,
                        file.TakeChannel().release());
  };
  fuchsia::io::FileSyncPtr file;
  zx_status_t status = open_at(path.data(), file.NewRequest());
  if (status != ZX_OK) {
    return status;
  }
  zx_status_t buffer_status;
  std::unique_ptr<fuchsia::mem::Buffer> buffer;
  status = file->GetBuffer(fuchsia::io::VMO_FLAG_READ, &buffer_status, &buffer);
  if (status != ZX_OK) {
    return status;
  } else if (buffer_status != ZX_OK) {
    return buffer_status;
  }
  std::string str;
  str.resize(buffer->size);
  status = buffer->vmo.read(str.data(), 0, buffer->size);
  if (status != ZX_OK) {
    return status;
  }
  return guest_config::ParseConfig(str, std::move(open_at), cfg);
}

class ServiceProviderImpl : public fuchsia::sys::ServiceProvider,
                            public fuchsia::virtualization::LaunchInfoProvider {
 public:
  static void CreateAndServe(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request,
                             fuchsia::io::DirectoryHandle pkg_dir,
                             fuchsia::io::DirectoryHandle svc_dir) {
    new ServiceProviderImpl(std::move(request), std::move(pkg_dir), std::move(svc_dir));
  }

 private:
  ServiceProviderImpl(fidl::InterfaceRequest<fuchsia::sys::ServiceProvider> request,
                      fuchsia::io::DirectoryHandle pkg_dir, fuchsia::io::DirectoryHandle svc_dir)
      : service_provider_binding_(this, std::move(request)),
        pkg_dir_(std::move(pkg_dir)),
        svc_dir_(std::move(svc_dir)) {
    // The service provider will self-delete on disconnection.
    service_provider_binding_.set_error_handler([this](...) { delete this; });
  }

  // |fuchsia::sys::ServiceProvider|
  void ConnectToService(std::string service_name, zx::channel channel) override {
    if (service_name == fuchsia::virtualization::LaunchInfoProvider::Name_) {
      fidl::InterfaceRequest<fuchsia::virtualization::LaunchInfoProvider> request(
          std::move(channel));
      bindings_.AddBinding(this, std::move(request));
    }
  }

  // |fuchsia::virtualization::LaunchInfoProvider|
  void Get(GetCallback callback) override {
    fuchsia::virtualization::LaunchInfo launch_info;
    // Read configuration provided by the component that launched us.
    fuchsia::virtualization::LaunchInfoProviderSyncPtr provider;
    zx_status_t status = fdio_service_connect_at(svc_dir_.channel().get(),
                                                 fuchsia::virtualization::LaunchInfoProvider::Name_,
                                                 provider.NewRequest().TakeChannel().release());
    if (status == ZX_OK) {
      // Get returns a status code, but failure is non-fatal, so ignore it.
      /* status = */ provider->Get(&launch_info);
    }
    auto cfg = &launch_info.guest_config;
    auto block_devices = std::move(*cfg->mutable_block_devices());
    // Read configuration from the guest's package.
    status = ReadGuestCfg(pkg_dir_, "data/guest.cfg", cfg);
    if (status != ZX_OK) {
      FX_LOGS(WARNING) << "Failed to read guest configuration";
    }
    // Make sure that block devices provided by the configuration in the guest's
    // package take precedence, as the order matters.
    for (auto& block_device : block_devices) {
      cfg->mutable_block_devices()->emplace_back(std::move(block_device));
    }
    // Merge the command-line additions into the main kernel command-line field.
    for (auto& cmdline : *cfg->mutable_cmdline_add()) {
      cfg->mutable_cmdline()->append(" " + cmdline);
    }
    cfg->clear_cmdline_add();
    // Set any defaults, before returning the configuration.
    guest_config::SetDefaults(cfg);
    callback(std::move(launch_info));
  }

  fidl::Binding<fuchsia::sys::ServiceProvider> service_provider_binding_;
  fidl::BindingSet<fuchsia::virtualization::LaunchInfoProvider> bindings_;
  fuchsia::io::DirectoryHandle pkg_dir_;
  fuchsia::io::DirectoryHandle svc_dir_;
};

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
    fuchsia::io::DirectoryHandle pkg_dir;
    fuchsia::io::DirectoryHandle svc_dir;

    // Create a bridge between directory_request that we got and VMM's
    // directory_request.
    fidl::InterfaceHandle<fuchsia::io::Directory> outgoing_dir;
    launch_info.directory_request = outgoing_dir.NewRequest().TakeChannel();
    fbl::RefPtr<fs::PseudoDir> dir = fbl::AdoptRef(new fs::PseudoDir());
    dir->AddEntry("svc", fbl::AdoptRef(new fs::RemoteDir(outgoing_dir.TakeChannel())));
    vfs_.ServeDirectory(std::move(dir), std::move(startup_info.launch_info.directory_request));

    // Pass-through some arguments directly to the VMM package.
    launch_info.url = "fuchsia-pkg://fuchsia.com/vmm#meta/vmm.cmx";
    launch_info.arguments = std::move(startup_info.launch_info.arguments);
    launch_info.flat_namespace = fuchsia::sys::FlatNamespace::New();
    for (size_t i = 0; i < startup_info.flat_namespace.paths.size(); ++i) {
      auto& path = startup_info.flat_namespace.paths[i];
      auto& directory = startup_info.flat_namespace.directories[i];
      if (path == "/pkg") {
        pkg_dir.set_channel(std::move(directory));
      } else if (path == "/svc") {
        svc_dir.set_channel(std::move(directory));
      }
    }

    // We list the LaunchInfoProvider service, so that the VMM can connect back
    // to the guest runner in order to get its LaunchInfo.
    auto service_list = fuchsia::sys::ServiceList::New();
    service_list->names.emplace_back(fuchsia::virtualization::LaunchInfoProvider::Name_);
    ServiceProviderImpl::CreateAndServe(service_list->provider.NewRequest(), std::move(pkg_dir),
                                        std::move(svc_dir));
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
  syslog::SetTags({"guest_runner"});
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  RunnerImpl runner(loop.dispatcher());
  return loop.Run();
}
