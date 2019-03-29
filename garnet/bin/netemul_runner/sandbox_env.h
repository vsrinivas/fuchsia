// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_
#define GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_

#include "src/lib/files/unique_fd.h"
#include <src/lib/fxl/macros.h>
#include <lib/netemul/network/network_context.h>
#include <lib/netemul/sync/sync_manager.h>
#include <memory>
#include <string>

namespace netemul {
class SandboxEnv {
 public:
  using Ptr = std::shared_ptr<SandboxEnv>;

  // Creates a sandbox environment
  SandboxEnv() = default;

  const std::string& default_name() const { return default_name_; }
  void set_default_name(std::string default_name) {
    default_name_ = std::move(default_name);
  }

  NetworkContext& network_context() { return net_context_; }
  SyncManager& sync_manager() { return sync_manager_; }

 private:
  std::string default_name_;
  NetworkContext net_context_;
  SyncManager sync_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SandboxEnv);
};
}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_
