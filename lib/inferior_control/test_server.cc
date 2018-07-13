// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_server.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <zircon/processargs.h>
#include <zircon/syscalls.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/sysinfo.h"
#include "garnet/lib/debugger_utils/util.h"

#include "gtest/gtest.h"

#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace debugserver {

TestServer::TestServer() : Server(GetRootJob(), GetDefaultJob()) {}

void TestServer::SetUp() {
  ASSERT_TRUE(exception_port_.Run());
  exception_port_started_ = true;
}

void TestServer::TearDown() {
  if (exception_port_started_) {
    // Tell the exception port to quit and wait for it to finish.
    exception_port_.Quit();
    exception_port_started_ = false;
  }

  EXPECT_TRUE(run_status_);
}

bool TestServer::Run() {
  // Start the main loop.
  message_loop_.Run();

  FXL_LOG(INFO) << "Main loop exited";

  // |run_status_| is checked by TearDown().
  return true;
}

bool TestServer::SetupInferior(const std::vector<std::string>& argv) {
  auto inferior = new debugserver::Process(this, this);
  inferior->set_argv(argv);
  // We take over ownership of |inferior| here.
  set_current_process(inferior);
  return true;
}

bool TestServer::RunHelperProgram(zx::channel channel) {
  Process* process = current_process();
  const Argv& argv = process->argv();

  FXL_LOG(INFO) << "Starting program: " << argv[0];

  if (channel.is_valid()) {
    process->AddStartupHandle({
        .id = PA_HND(PA_USER0, 0),
        .handle = std::move(channel),
    });
  }

  if (!process->Initialize()) {
    FXL_LOG(ERROR) << "failed to set up inferior";
    return false;
  }

  FXL_DCHECK(!process->IsLive());
  if (!process->Start()) {
    FXL_LOG(ERROR) << "failed to start process";
    return false;
  }
  FXL_DCHECK(process->IsLive());

  return true;
}

// This method is intended to be called at the end of tests.
// There are several things we check for successful exit, and it's easier
// to have them all in one place. Note that we use gtest macros instead of
// FXL_DCHECK because these all verify test conditions.
bool TestServer::TestSuccessfulExit() {
  auto inferior = current_process();
  if (inferior == nullptr) {
    FXL_LOG(ERROR) << "inferior == nullptr";
    return false;
  }
  if (inferior->IsAttached()) {
    FXL_LOG(ERROR) << "inferior still attached";
    return false;
  }
  if (inferior->IsLive()) {
    FXL_LOG(ERROR) << "inferior still live";
    return false;
  }
  // We can't get the exit code from |inferior| as we've detached. Instead
  // we save it on process exit.
  if (!exit_code_set_ || exit_code_ != 0) {
    FXL_LOG(ERROR) << "inferior didn't cleanly exit";
    return false;
  }
  return true;
}

void TestServer::OnThreadStarting(Process* process, Thread* thread,
                                  const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  PrintException(stdout, thread, ZX_EXCP_THREAD_STARTING, context);

  switch (process->state()) {
    case Process::State::kStarting:
    case Process::State::kRunning:
      break;
    default:
      FXL_DCHECK(false);
  }

  thread->Resume();
}

void TestServer::OnThreadExiting(Process* process, Thread* thread,
                                 const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  PrintException(stdout, thread, ZX_EXCP_THREAD_EXITING, context);

  // We still have to "resume" the thread so that the o/s will complete the
  // termination of the thread.
  thread->ResumeForExit();
}

void TestServer::OnProcessExit(Process* process) {
  FXL_DCHECK(process);

  // Save the exit code for later testing.
  exit_code_ = process->ExitCode();
  exit_code_set_ = true;

  printf("Process %s is gone, rc %d\n", process->GetName().c_str(), exit_code_);

  // If the process is gone exit main loop.
  QuitMessageLoop(true);
}

void TestServer::OnArchitecturalException(
    Process* process, Thread* thread, const zx_excp_type_t type,
    const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  PrintException(stdout, thread, type, context);

  QuitMessageLoop(true);
}

void TestServer::OnSyntheticException(Process* process, Thread* thread,
                                      zx_excp_type_t type,
                                      const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  PrintException(stdout, thread, type, context);

  QuitMessageLoop(true);
}

}  // namespace debugserver
