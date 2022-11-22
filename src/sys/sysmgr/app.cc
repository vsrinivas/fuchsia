// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/sysmgr/app.h"

#include <fuchsia/io/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async/default.h>
#include <lib/async/dispatcher.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/vfs/cpp/service.h>
#include <zircon/process.h>
#include <zircon/processargs.h>

namespace sysmgr {
namespace {
constexpr char kDefaultLabel[] = "sys";
}  // namespace

App::App(bool auto_update_packages, std::shared_ptr<sys::ServiceDirectory> incoming_services,
         async::Loop* loop)
    : loop_(loop),
      incoming_services_(std::move(incoming_services)),
      auto_updates_enabled_(auto_update_packages) {
  auto env_request = env_.NewRequest();
  fuchsia::sys::ServiceProviderPtr env_services;
  env_->GetServices(env_services.NewRequest());
  fidl::InterfaceRequest<fuchsia::io::Directory> directory;
  env_->GetDirectory(std::move(directory));

  // Configure loader.
  if (auto_updates_enabled_) {
    package_updating_loader_ = std::make_unique<PackageUpdatingLoader>(
        std::move(env_services), async_get_default_dispatcher());
  }
  static const char* const kLoaderName = fuchsia::sys::Loader::Name_;
  auto child =
      std::make_unique<vfs::Service>([this](zx::channel channel, async_dispatcher_t* dispatcher) {
        if (auto_updates_enabled_) {
          package_updating_loader_->Bind(
              fidl::InterfaceRequest<fuchsia::sys::Loader>(std::move(channel)));
        } else {
          incoming_services_->Connect(kLoaderName, std::move(channel));
        }
      });
  svc_names_.push_back(kLoaderName);
  svc_root_.AddEntry(kLoaderName, std::move(child));

  // Set up environment for the programs we will run.
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names = std::move(svc_names_);
  service_list->host_directory = OpenAsDirectory();
  fuchsia::sys::EnvironmentPtr environment;
  incoming_services_->Connect(environment.NewRequest());
  // Inherit services from the root appmgr realm, which includes certain
  // services currently implemented by non-component processes that are passed
  // through appmgr to this sys realm. Note that |service_list| will override
  // the inherited services if it includes services also in the root realm.
  fuchsia::sys::EnvironmentOptions options = {.inherit_parent_services = true};
  environment->CreateNestedEnvironment(std::move(env_request), env_controller_.NewRequest(),
                                       kDefaultLabel, std::move(service_list), std::move(options));
}

App::~App() = default;

fidl::InterfaceHandle<fuchsia::io::Directory> App::OpenAsDirectory() {
  fidl::InterfaceHandle<fuchsia::io::Directory> dir;
  svc_root_.Serve(fuchsia::io::OpenFlags::RIGHT_READABLE | fuchsia::io::OpenFlags::RIGHT_WRITABLE,
                  dir.NewRequest().TakeChannel());
  return dir;
}

}  // namespace sysmgr
