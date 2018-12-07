#include <utility>

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_
#define GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_

#include <lib/fxl/files/unique_fd.h>
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

 private:
  std::string package_name_;
  fxl::UniqueFD package_dir_;
};
}  // namespace netemul

#endif  // GARNET_BIN_NETEMUL_RUNNER_SANDBOX_ENV_H_
