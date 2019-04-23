// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_ENV_H_
#define SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_ENV_H_

#include <src/connectivity/network/testing/netemul/lib/network/network_context.h>
#include <src/connectivity/network/testing/netemul/lib/sync/sync_manager.h>
#include <src/lib/fxl/macros.h>

#include <memory>
#include <string>

#include "src/lib/files/unique_fd.h"

namespace netemul {
class SandboxEnv {
 public:
  using Ptr = std::shared_ptr<SandboxEnv>;
  class Events {
   public:
    Events() = default;
    Events(Events&& other) = default;

   public:
    fit::function<void(const std::string&, int64_t,
                       fuchsia::sys::TerminationReason)>
        service_terminated;
    FXL_DISALLOW_COPY_AND_ASSIGN(Events);
  };

  // Creates a sandbox environment
  explicit SandboxEnv(Events events = Events()) : events_(std::move(events)) {}

  const std::string& default_name() const { return default_name_; }
  void set_default_name(std::string default_name) {
    default_name_ = std::move(default_name);
  }

  const Events& events() const { return events_; }

  NetworkContext& network_context() { return net_context_; }
  SyncManager& sync_manager() { return sync_manager_; }

 private:
  std::string default_name_;
  Events events_;
  NetworkContext net_context_;
  SyncManager sync_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SandboxEnv);
};
}  // namespace netemul

#endif  // SRC_CONNECTIVITY_NETWORK_TESTING_NETEMUL_RUNNER_SANDBOX_ENV_H_
