// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "server.h"

#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include <launchpad/launchpad.h>
#include <magenta/syscalls.h>

#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_printf.h"

#include "debugger-utils/util.h"

#include "control.h"

static constexpr char ldso_trace_env_var[] = "LD_TRACE_FILE";
static constexpr char ldso_trace_output_path[] = "/tmp/ptout.ldso";

namespace debugserver {

IptServer::IptServer(const IptConfig& config)
  : config_(config) {
}

bool IptServer::StartInferior() {
  Process* process = current_process();
  const util::Argv& argv = process->argv();

  FTL_LOG(INFO) << "Starting program: " << argv[0];

  if (!SetPerfMode(config_))
    return false;

  // We need details of where the program and its dsos are loaded.
  // This data is obtained from the dynamic linker.
  // TODO(dje): Is there a better way?
  setenv(ldso_trace_env_var, ldso_trace_output_path, 1);

  if (config_.mode == IPT_MODE_CPUS) {
    if (!InitCpuPerf(config_))
      return false;
  }

  if (!InitPerfPreProcess(config_))
    return false;

  // N.B. It's important that the PT device be closed at this point as we
  // don't want the inferior to inherit the open descriptor: the device can
  // only be opened once at a time.

  if (!process->Initialize()) {
    util::LogError("failed to set up inferior");
    return false;
  }

  FTL_DCHECK(!process->IsAttached());
  if (!process->Attach()) {
    util::LogError("failed to attach to process");
    return false;
  }
  FTL_DCHECK(process->IsAttached());

  // If tracing cpus, defer turning on tracing as long as possible so that we
  // don't include all the initialization. For threads it doesn't matter.
  // TODO(dje): Could even defer until the first thread is started.
  if (config_.mode == IPT_MODE_CPUS) {
    if (!StartCpuPerf(config_)) {
      ResetPerf(config_);
      return false;
    }
  }

  FTL_DCHECK(!process->IsLive());
  if (!process->Start()) {
    util::LogError("failed to start process");
    return false;
  }
  FTL_DCHECK(process->IsLive());

  return true;
}

bool IptServer::DumpResults() {
  if (config_.mode == IPT_MODE_CPUS)
    StopCpuPerf(config_);
  StopPerf(config_);
  if (config_.mode == IPT_MODE_CPUS)
    DumpCpuPerf(config_);
  DumpPerf(config_);
  if (config_.mode == IPT_MODE_CPUS)
    ResetCpuPerf(config_);
  ResetPerf(config_);
  return true;
}

bool IptServer::Run() {
  FTL_DCHECK(!io_loop_);

  if (!exception_port_.Run()) {
    FTL_LOG(ERROR) << "Failed to initialize exception port!";
    return false;
  }

  if (!StartInferior()) {
    FTL_LOG(ERROR) << "Failed to start inferior";
    return false;
  }

  // Start the main loop.
  message_loop_.Run();

  FTL_LOG(INFO) << "Main loop exited";

  // Tell the exception port to quit and wait for it to finish.
  exception_port_.Quit();

  if (!DumpResults()) {
    FTL_LOG(ERROR) << "Error dumping results";
    return false;
  }

  return run_status_;
}

void IptServer::OnBytesRead(const ftl::StringView& bytes_read) {
  // TODO(dje): Do we need an i/o loop?
}

void IptServer::OnDisconnected() {
  // TODO(dje): Do we need an i/o loop?
}

void IptServer::OnIOError() {
  // TODO(dje): Do we need an i/o loop?
}

void IptServer::OnThreadStarting(Process* process,
                                 Thread* thread,
                                 const mx_exception_context_t& context) {
  FTL_DCHECK(process);
  FTL_DCHECK(thread);

  PrintException(stdout, process, thread, MX_EXCP_THREAD_STARTING, context);

  switch (process->state()) {
  case Process::State::kStarting:
  case Process::State::kRunning:
    break;
  default:
    FTL_DCHECK(false);
  }

  if (config_.mode == IPT_MODE_THREADS) {
    if (!InitThreadPerf(thread, config_))
      goto Fail;
    if (!StartThreadPerf(thread, config_)) {
      ResetThreadPerf(thread, config_);
      goto Fail;
    }
  }

 Fail:
  thread->Resume();
}

void IptServer::OnThreadExiting(Process* process,
                                Thread* thread,
                                const mx_excp_type_t type,
                                const mx_exception_context_t& context) {
  FTL_DCHECK(process);
  FTL_DCHECK(thread);

  PrintException(stdout, process, thread, type, context);

  // Dump any collected trace.
  if (config_.mode == IPT_MODE_THREADS) {
    if (thread->ipt_buffer() >= 0) {
      StopThreadPerf(thread, config_);
      DumpThreadPerf(thread, config_);
      ResetThreadPerf(thread, config_);
    }
  }

  // We still have to "resume" the thread so that the o/s will complete the
  // termination of the thread.
  thread->Resume();
}

void IptServer::OnProcessExit(Process* process,
                              const mx_excp_type_t type,
                              const mx_exception_context_t& context) {
  FTL_DCHECK(process);

  PrintException(stdout, process, nullptr, type, context);

  // If the process is gone, unset current thread, and exit main loop.
  SetCurrentThread(nullptr);
  QuitMessageLoop(true);
}

void IptServer::OnArchitecturalException(Process* process,
                                         Thread* thread,
                                         const mx_excp_type_t type,
                                         const mx_exception_context_t& context) {
  FTL_DCHECK(process);
  FTL_DCHECK(thread);
  // TODO(armansito): Fine-tune this check if we ever support multi-processing.
  FTL_DCHECK(process == current_process());

  PrintException(stdout, process, thread, type, context);

  // This is generally a segv or some such. Not much we can do.
  QuitMessageLoop(true);
}

}  // namespace debugserver
