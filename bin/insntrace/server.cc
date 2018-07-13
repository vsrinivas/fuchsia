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

#include <zircon/syscalls.h>

#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/sysinfo.h"
#include "garnet/lib/debugger_utils/util.h"

#include "control.h"

namespace debugserver {

constexpr char IptConfig::kDefaultOutputPathPrefix[];

IptConfig::IptConfig()
    : mode(kDefaultMode),
      num_cpus(zx_system_get_num_cpus()),
      max_threads(kDefaultMaxThreads),
      num_chunks(kDefaultNumChunks),
      chunk_order(kDefaultChunkOrder),
      is_circular(kDefaultIsCircular),
      branch(true),
      cr3_match(0),
      cr3_match_set(false),
      cyc(false),
      cyc_thresh(0),
      mtc(false),
      mtc_freq(0),
      psb_freq(0),
      os(true),
      user(true),
      retc(true),
      tsc(true),
      output_path_prefix(kDefaultOutputPathPrefix) {
  addr[0] = AddrFilter::kOff;
  addr[1] = AddrFilter::kOff;
}

uint64_t IptConfig::CtlMsr() const {
  uint64_t msr = 0;

  // For documentation of the fields see the description of the IA32_RTIT_CTL
  // MSR in chapter 36 "Intel Processor Trace" of Intel Volume 3.

  if (cyc)
    msr |= 1 << 1;
  if (os)
    msr |= 1 << 2;
  if (user)
    msr |= 1 << 3;
  if (cr3_match)
    msr |= 1 << 7;
  if (mtc)
    msr |= 1 << 9;
  if (tsc)
    msr |= 1 << 10;
  if (!retc)
    msr |= 1 << 11;
  if (branch)
    msr |= 1 << 13;
  msr |= (mtc_freq & 15) << 14;
  msr |= (cyc_thresh & 15) << 19;
  msr |= (psb_freq & 15) << 24;
  msr |= (uint64_t)addr[0] << 32;
  msr |= (uint64_t)addr[1] << 36;

  return msr;
}

uint64_t IptConfig::AddrBegin(unsigned i) const {
  FXL_DCHECK(i < arraysize(addr_range));
  return addr_range[i].begin;
}

uint64_t IptConfig::AddrEnd(unsigned i) const {
  FXL_DCHECK(i < arraysize(addr_range));
  return addr_range[i].end;
}

IptServer::IptServer(const IptConfig& config)
    : Server(GetRootJob(), GetDefaultJob()), config_(config) {}

bool IptServer::StartInferior() {
  Process* process = current_process();
  const Argv& argv = process->argv();

  FXL_LOG(INFO) << "Starting program: " << argv[0];

  if (!AllocTrace(config_))
    return false;

  if (config_.mode == IPT_MODE_CPUS) {
    if (!InitCpuPerf(config_))
      goto Fail;
  }

  if (!InitPerfPreProcess(config_))
    goto Fail;

  // N.B. It's important that the PT device be closed at this point as we
  // don't want the inferior to inherit the open descriptor: the device can
  // only be opened once at a time.

  if (!process->Initialize()) {
    FXL_LOG(ERROR) << "failed to set up inferior";
    goto Fail;
  }

  if (!config_.cr3_match_set) {
    // TODO(dje): fetch cr3 for inferior and apply it to cr3_match
  }

  // If tracing cpus, defer turning on tracing as long as possible so that we
  // don't include all the initialization. For threads it doesn't matter.
  // TODO(dje): Could even defer until the first thread is started.
  if (config_.mode == IPT_MODE_CPUS) {
    if (!StartCpuPerf(config_))
      goto Fail;
  }

  FXL_DCHECK(!process->IsLive());
  if (!process->Start()) {
    FXL_LOG(ERROR) << "failed to start process";
    if (config_.mode == IPT_MODE_CPUS)
      StopCpuPerf(config_);
    goto Fail;
  }
  FXL_DCHECK(process->IsLive());

  return true;

Fail:
  FreeTrace(config_);
  return false;
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
  FreeTrace(config_);
  return true;
}

bool IptServer::Run() {
  if (!exception_port_.Run()) {
    FXL_LOG(ERROR) << "Failed to initialize exception port!";
    return false;
  }

  if (!StartInferior()) {
    FXL_LOG(ERROR) << "Failed to start inferior";
    return false;
  }

  // Start the main loop.
  message_loop_.Run();

  FXL_LOG(INFO) << "Main loop exited";

  // Tell the exception port to quit and wait for it to finish.
  exception_port_.Quit();

  if (!DumpResults()) {
    FXL_LOG(ERROR) << "Error dumping results";
    return false;
  }

  return run_status_;
}

void IptServer::OnThreadStarting(Process* process, Thread* thread,
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

void IptServer::OnThreadExiting(Process* process, Thread* thread,
                                const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  PrintException(stdout, thread, ZX_EXCP_THREAD_EXITING, context);

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
  thread->ResumeForExit();
}

void IptServer::OnProcessExit(Process* process) {
  FXL_DCHECK(process);

  printf("Process %s is gone, rc %d\n", process->GetName().c_str(),
         process->ExitCode());

  // If the process is gone, unset current thread, and exit main loop.
  SetCurrentThread(nullptr);
  QuitMessageLoop(true);
}

void IptServer::OnArchitecturalException(
    Process* process, Thread* thread, const zx_excp_type_t type,
    const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);
  // TODO(armansito): Fine-tune this check if we ever support multi-processing.
  FXL_DCHECK(process == current_process());

  PrintException(stdout, thread, type, context);

  // This is generally a segv or some such. Not much we can do.
  QuitMessageLoop(true);
}

void IptServer::OnSyntheticException(Process* process, Thread* thread,
                                     zx_excp_type_t type,
                                     const zx_exception_context_t& context) {
  FXL_DCHECK(process);
  FXL_DCHECK(thread);

  PrintException(stdout, thread, type, context);

  // Program is crashing.
  QuitMessageLoop(true);
}

}  // namespace debugserver
