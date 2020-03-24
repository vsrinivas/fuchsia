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
constexpr const char* kNetworkTunUrl = "fuchsia-pkg://fuchsia.com/network-tun#meta/network-tun.cmx";

class ServiceHolder {
 public:
  ServiceHolder(const char* url, sys::ServiceDirectory* services,
                fit::function<void(zx_status_t)> error_callback) {
    fidl::InterfacePtr<fuchsia::sys::Launcher> launcher;
    services->Connect(launcher.NewRequest());
    fuchsia::sys::LaunchInfo info{};
    info.url = url;

    directory_ = sys::ServiceDirectory::CreateWithRequest(&info.directory_request);

    launcher->CreateComponent(std::move(info), ctlr_.NewRequest());
    ctlr_.set_error_handler(std::move(error_callback));
  }

  template <typename T>
  void Connect(fidl::InterfaceRequest<T> req) {
    directory_->Connect(std::move(req));
  }

 private:
  std::shared_ptr<sys::ServiceDirectory> directory_;
  fidl::InterfacePtr<fuchsia::sys::ComponentController> ctlr_;
};

void SandboxEnv::ConnectDevfs(zx::channel req) {
  if (!devfs_) {
    ZX_ASSERT(env_services_);
    // Create devfs holder lazily.
    devfs_ = std::make_unique<ServiceHolder>(kDevmgrUrl, env_services_.get(), [this](zx_status_t) {
      if (events_.devfs_terminated) {
        events_.devfs_terminated();
      }
      devfs_ = nullptr;
    });
  }

  devfs_->Connect(fidl::InterfaceRequest<fuchsia::netemul::devmgr::IsolatedDevmgr>(std::move(req)));
}

void SandboxEnv::ConnectNetworkTun(fidl::InterfaceRequest<fuchsia::net::tun::Control> req) {
  if (!network_tun_) {
    ZX_ASSERT(env_services_);
    // Create network_tun holder lazily.
    network_tun_ =
        std::make_unique<ServiceHolder>(kNetworkTunUrl, env_services_.get(), [this](zx_status_t) {
          if (events_.network_tun_terminated) {
            events_.network_tun_terminated();
          }
          network_tun_ = nullptr;
        });
  }

  network_tun_->Connect(std::move(req));
}

SandboxEnv::SandboxEnv(std::shared_ptr<sys::ServiceDirectory> env_services,
                       SandboxEnv::Events events)
    : env_services_(std::move(env_services)), events_(std::move(events)) {
  net_context_.SetNetworkTunHandler(fit::bind_member(this, &SandboxEnv::ConnectNetworkTun));
}

void SandboxEnv::set_devfs_enabled(bool enabled) {
  if (enabled) {
    net_context_.SetDevfsHandler([this](zx::channel req) { ConnectDevfs(std::move(req)); });
  } else {
    // prevent users from toggling enabling/disabling this if devfs
    // was already created. It'd make for very confusing use cases.
    ZX_ASSERT(!devfs_);
    net_context_.SetDevfsHandler(nullptr);
  }
}

SandboxEnv::~SandboxEnv() = default;

}  // namespace netemul
