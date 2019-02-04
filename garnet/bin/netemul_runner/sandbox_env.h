// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_
#define GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_

#include <lib/fxl/files/unique_fd.h>
#include <lib/fxl/macros.h>
#include <lib/netemul/network/network_context.h>
#include <lib/netemul/sync/sync_manager.h>
#include <memory>
#include <string>

namespace netemul {
class SandboxEnv {
 public:
  using Ptr = std::shared_ptr<SandboxEnv>;

  SandboxEnv(std::string package_name, fxl::UniqueFD dir)
      : package_name_(std::move(package_name)), package_dir_(std::move(dir)) {}

  const std::string& name() const { return package_name_; }

  const fxl::UniqueFD& dir() const { return package_dir_; }

  NetworkContext& network_context() { return net_context_; }
  SyncManager& sync_manager() { return sync_manager_; }

 private:
  std::string package_name_;
  fxl::UniqueFD package_dir_;
  NetworkContext net_context_;
  SyncManager sync_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SandboxEnv);
};
}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_
