// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <inspector/inspector.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

ProcessProxyImpl::ProcessProxyImpl(uint64_t target_id, const std::shared_ptr<ModulePool>& pool)
    : target_id_(target_id), pool_(std::move(pool)) {}

ProcessProxyImpl::~ProcessProxyImpl() {
  // Prevent new signals and/or errors.
  closed_ = true;
  coordinator_.Reset();
  // Interrupt associated workflows.
  process_.kill();
  exception_channel_.reset();
  // ...and join them.
  Waiter waiter = [this](zx::time deadline) {
    return process_.wait_one(ZX_PROCESS_TERMINATED, deadline, nullptr);
  };
  WaitFor("process to terminate", &waiter);
  if (exception_thread_.joinable()) {
    exception_thread_.join();
  }
  // Deregister this object's shared memory objects from the module pool.
  for (auto& kv : modules_) {
    auto* module_proxy = kv.first;
    auto& counters = kv.second;
    module_proxy->Remove(counters.data());
  }
}

///////////////////////////////////////////////////////////////
// Configuration methods

void ProcessProxyImpl::AddDefaults(Options* options) {
  if (!options->has_malloc_exitcode()) {
    options->set_malloc_exitcode(kDefaultMallocExitcode);
  }
  if (!options->has_death_exitcode()) {
    options->set_death_exitcode(kDefaultDeathExitcode);
  }
  if (!options->has_leak_exitcode()) {
    options->set_leak_exitcode(kDefaultLeakExitcode);
  }
  if (!options->has_oom_exitcode()) {
    options->set_oom_exitcode(kDefaultOomExitcode);
  }
}

void ProcessProxyImpl::Configure(const std::shared_ptr<Options>& options) { options_ = options; }

void ProcessProxyImpl::SetHandlers(SignalHandler on_signal, ErrorHandler on_error) {
  FX_DCHECK(!on_signal_);
  FX_DCHECK(!on_error_);
  on_signal_ = std::move(on_signal);
  on_error_ = std::move(on_error);
}

void ProcessProxyImpl::Connect(InstrumentedProcess instrumented) {
  if (closed_) {
    return;
  }
  FX_DCHECK(options_);
  FX_DCHECK(on_signal_);
  FX_DCHECK(on_error_);
  // Create exception channel. Do this before starting the signal handler in case it errors
  // immediately.
  auto* process = instrumented.mutable_process();
  process_ = std::move(*process);
  auto status = process_.create_exception_channel(0, &exception_channel_);
  if (status != ZX_OK) {
    // The process already crashed!
    FX_CHECK(status == ZX_ERR_BAD_STATE);
    result_ = FuzzResult::CRASH;
  }
  // Start crash handler.
  FX_CHECK(!exception_thread_.joinable());
  exception_thread_ = std::thread([this]() {
    zx_exception_info_t info;
    zx::exception exception;
    Waiter waiter = [this](zx::time deadline) {
      return exception_channel_.wait_one(ZX_CHANNEL_READABLE, deadline, nullptr);
    };
    if (WaitFor("exception", &waiter) != ZX_OK ||
        exception_channel_.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                nullptr, nullptr) != ZX_OK) {
      // Process exited and channel was closed before or during the wait and/or read.
      // |GetResult| will attempt to determine the reason using the exitcode.
      return;
    }
    result_ = FuzzResult::CRASH;
  });
  // Wire up signal forwarders.
  auto* eventpair = instrumented.mutable_eventpair();
  coordinator_.Pair(std::move(*eventpair), [this](zx_signals_t observed) {
    switch (observed) {
      case kStart:
        break;
      case kFinish:
        leak_suspected_ = false;
        break;
      case kFinishWithLeaks:
        leak_suspected_ = true;
        break;
      default:
        if (closed_) {
          // no-op
        } else if (observed & ZX_EVENTPAIR_PEER_CLOSED) {
          // If the peer exits, invoke the error handler.
          on_error_(target_id_);
        } else {
          FX_LOGS(ERROR) << "ProcessProxy received unknown signal: 0x" << std::hex << observed;
        }
        return false;
    }
    on_signal_();
    return true;
  });
  coordinator_.SignalPeer(kSync);
}

void ProcessProxyImpl::AddLlvmModule(LlvmModule llvm_module) {
  if (closed_) {
    return;
  }
  SharedMemory counters;
  auto* inline_8bit_counters = llvm_module.mutable_inline_8bit_counters();
  counters.LinkMirrored(std::move(*inline_8bit_counters));
  auto* module_proxy = pool_->Get(llvm_module.id(), counters.size());
  module_proxy->Add(counters.data(), counters.size());
  modules_[module_proxy] = std::move(counters);
  coordinator_.SignalPeer(kSync);
}

///////////////////////////////////////////////////////////////
// Run-related methods

void ProcessProxyImpl::Start(bool detect_leaks) {
  leak_suspected_ = false;
  coordinator_.SignalPeer(detect_leaks ? kStartLeakCheck : kStart);
}

void ProcessProxyImpl::Finish() { coordinator_.SignalPeer(kFinish); }

///////////////////////////////////////////////////////////////
// Status-related methods.

zx_status_t ProcessProxyImpl::GetStats(ProcessStats* out) {
  return GetStatsForProcess(process_, out);
}

FuzzResult ProcessProxyImpl::GetResult() {
  FX_DCHECK(options_);
  Waiter waiter = [this](zx::time deadline) {
    return process_.wait_one(ZX_PROCESS_TERMINATED, deadline, nullptr);
  };
  auto status = WaitFor("process to terminate", &waiter);
  FX_CHECK(status == ZX_OK) << "failed to terminate process: " << zx_status_get_string(status);
  zx_info_process_t info;
  status = process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info), nullptr, nullptr);
  FX_CHECK(status == ZX_OK) << "failed to get process info: " << zx_status_get_string(status);
  FX_CHECK(info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
  FuzzResult result = FuzzResult::NO_ERRORS;
  if (info.return_code == options_->malloc_exitcode()) {
    result = FuzzResult::BAD_MALLOC;
  } else if (info.return_code == options_->death_exitcode()) {
    result = FuzzResult::DEATH;
  } else if (info.return_code == options_->leak_exitcode()) {
    result = FuzzResult::LEAK;
  } else if (info.return_code == options_->oom_exitcode()) {
    result = FuzzResult::OOM;
  } else if (info.return_code != 0) {
    result = FuzzResult::EXIT;
  }
  // Set the result, unless it was already set.
  FuzzResult previous = FuzzResult::NO_ERRORS;
  return result_.compare_exchange_strong(previous, result) ? result : previous;
}

size_t ProcessProxyImpl::Dump(void* buf, size_t size) {
  FX_DCHECK(buf && size);
  auto* out = fmemopen(buf, size, "r+");
  char* str = reinterpret_cast<char*>(buf);
  if (!out) {
    FX_LOGS(ERROR) << "Cannot dump threads; fmemopen failed (errno=" << errno << ").";
    str[0] = 0;
    return 0;
  }
  inspector_print_debug_info_for_all_threads(out, process_.get());
  fclose(out);
  str[size - 1] = 0;
  return strlen(str);
}

}  // namespace fuzzing
