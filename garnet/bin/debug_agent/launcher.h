// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_DEBUG_AGENT_LAUNCHER_H_
#define GARNET_BIN_DEBUG_AGENT_LAUNCHER_H_

#include <string>
#include <vector>

#include <lib/zx/process.h>
#include "garnet/lib/process/process_builder.h"
#include "lib/sys/cpp/service_directory.h"

#include "lib/fxl/macros.h"

namespace debug_agent {

// This class is designed to help two-phase process creation, where a process
// needs to be setup, but before starting it that process needs to be
// registered with the exception handler.
//
// Launchpad and our calling code have different semantics which makes a bit
// of a mismatch. Launchpad normally expects to work by doing setup and then
// returning ownership of its internal process handle at the end of launching.
// But our code needs to set up the exception handling before code starts
// executing, and expects to own the handle its using.
class Launcher {
 public:
  explicit Launcher(std::shared_ptr<sys::ServiceDirectory> env_services);
  ~Launcher() = default;

  // Setup will create the process object but not launch the process yet.
  zx_status_t Setup(const std::vector<std::string>& argv);

  // Accessor for a copy of the process handle, valid between Setup() and
  // Start().
  zx::process GetProcess() const;

  // Completes process launching.
  zx_status_t Start();

 private:
  process::ProcessBuilder builder_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Launcher);
};

}  // namespace debug_agent

#endif  // GARNET_BIN_DEBUG_AGENT_LAUNCHER_H_
