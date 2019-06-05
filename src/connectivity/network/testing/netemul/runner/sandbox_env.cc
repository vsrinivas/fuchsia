// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sandbox_env.h"

#include <fuchsia/netemul/devmgr/cpp/fidl.h>
#include <lib/fdio/directory.h>
#include <lib/sys/cpp/service_directory.h>

namespace netemul {

constexpr const char* kDevmgrUrl =
    "fuchsia-pkg://fuchsia.com/netemul_devmgr#meta/netemul_devmgr.cmx";

class DevfsHolder {
 public:
  DevfsHolder(sys::ServiceDirectory& services,
              fit::function<void(zx_status_t)> error_callback) {
    fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
    services.Connect(launcher.NewRequest());
    fuchsia::sys::LaunchInfo info{};
    info.url = kDevmgrUrl;

    directory_ =
        sys::ServiceDirectory::CreateWithRequest(&info.directory_request);

    launcher->CreateComponent(std::move(info), ctlr_.NewRequest());
    ctlr_.set_error_handler(std::move(error_callback));
  }

  void Connect(zx::channel req) {
    directory_->Connect(
        fidl::InterfaceRequest<fuchsia::netemul::devmgr::IsolatedDevmgr>(
            std::move(req)));
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> directory_;
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr_;
};

void SandboxEnv::ConnectDevfs(zx::channel req) {
  if (!devfs_) {
    ZX_ASSERT(env_services_);
    // create devfs holder lazily
    devfs_ = std::make_unique<DevfsHolder>(*env_services_, [this](zx_status_t) {
      if (events_.devfs_terminated) {
        events_.devfs_terminated();
      }
      devfs_ = nullptr;
    });
  }

  devfs_->Connect(std::move(req));
}

SandboxEnv::SandboxEnv(std::shared_ptr<sys::ServiceDirectory> env_services,
                       SandboxEnv::Events events)
    : env_services_(std::move(env_services)), events_(std::move(events)) {}

void SandboxEnv::set_devfs_enabled(bool enabled) {
  if (enabled) {
    net_context_.SetDevfsHandler(
        [this](zx::channel req) { ConnectDevfs(std::move(req)); });
  } else {
    // prevent users from toggling enabling/disabling this if devfs
    // was already created. It'd make for very confusing use cases.
    ZX_ASSERT(!devfs_);
    net_context_.SetDevfsHandler(nullptr);
  }
}

SandboxEnv::~SandboxEnv() = default;

}  // namespace netemul
