// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_ENV_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_ENV_H_

#include <fuchsia/sys/cpp/fidl.h>

#include <memory>
#include <string>

#include <sdk/lib/sys/cpp/component_context.h>
#include <src/connectivity/network/testing/netemul/lib/network/network_context.h>
#include <src/connectivity/network/testing/netemul/lib/sync/sync_manager.h>

#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace netemul {

class ServiceHolder;
class ManagedEnvironment;

class SandboxEnv {
 public:
  using Ptr = std::shared_ptr<SandboxEnv>;
  class Events {
   public:
    Events() = default;
    Events(Events&& other) = default;

   public:
    fit::function<void(const std::string&, int64_t, fuchsia::sys::TerminationReason)>
        service_terminated;

    fit::function<void()> devfs_terminated;
    fit::function<void()> network_tun_terminated;

    FXL_DISALLOW_COPY_AND_ASSIGN(Events);
  };

  // Creates a sandbox environment
  SandboxEnv(std::shared_ptr<sys::ServiceDirectory> env_services, Events events = Events());
  ~SandboxEnv();

  const std::string& default_name() const { return default_name_; }
  void set_default_name(std::string default_name) { default_name_ = std::move(default_name); }

  void set_devfs_enabled(bool enabled);

  const Events& events() const { return events_; }

  NetworkContext& network_context() { return net_context_; }
  SyncManager& sync_manager() { return sync_manager_; }

  std::weak_ptr<ManagedEnvironment> guest_env_;

 private:
  void ConnectDevfs(zx::channel req);
  void ConnectNetworkTun(fidl::InterfaceRequest<fuchsia::net::tun::Control> req);
  std::string default_name_;
  std::shared_ptr<sys::ServiceDirectory> env_services_;
  std::unique_ptr<ServiceHolder> devfs_;
  std::unique_ptr<ServiceHolder> network_tun_;
  Events events_;
  NetworkContext net_context_;
  SyncManager sync_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SandboxEnv);
};
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_ENV_H_
