// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

#include <lib/zx/channel.h>
#include <zircon/syscalls.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_view.h"

#include "gtest/gtest.h"

#include "garnet/lib/inferior_control/exception_port.h"
#include "garnet/lib/inferior_control/process.h"
#include "garnet/lib/inferior_control/server.h"
#include "garnet/lib/inferior_control/thread.h"

namespace inferior_control {

// Server baseclass for tests. Tests may subclass this if they need.
// NOTE: This class is generally not thread safe. Care must be taken when
// calling methods which modify the internal state of a TestServer instance.
class TestServer : public Server, public ::testing::Test {
 public:
  TestServer();

  // ::testing::Test overrides
  void SetUp() override;
  void TearDown() override;

  // Server overrides
  bool Run() override;

  bool SetupInferior(const std::vector<std::string>& argv);
  bool RunHelperProgram(zx::channel channel);
  bool TestSuccessfulExit();

  int exit_code() const { return exit_code_; }
  bool exit_code_set() const { return exit_code_set_; }

 protected:
  // Process::Delegate overrides.
  void OnThreadStarting(Process* process, Thread* thread,
                        const zx_exception_context_t& context) override;
  void OnThreadExiting(Process* process, Thread* thread,
                       const zx_exception_context_t& context) override;
  void OnProcessExit(Process* process) override;
  void OnArchitecturalException(Process* process, Thread* thread,
                                const zx_excp_type_t type,
                                const zx_exception_context_t& context) override;
  void OnSyntheticException(Process* process, Thread* thread,
                            zx_excp_type_t type,
                            const zx_exception_context_t& context) override;

 private:
  // Processes are detached from when they exit.
  // Save the exit code for later testing.
  int exit_code_ = -1;
  bool exit_code_set_ = false;

  // exception_port_.Quit() can only be called after a successful call to
  // exception_port_.Run(), so keep track of whether Run() succeeded.
  bool exception_port_started_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestServer);
};

}  // namespace inferior_control
