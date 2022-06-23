// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <inspector/inspector.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

ProcessProxy::ProcessProxy(ExecutorPtr executor, uint64_t target_id, const ModulePoolPtr& pool)
    : executor_(executor), target_id_(target_id), eventpair_(executor), pool_(std::move(pool)) {
  FX_DCHECK(executor);
}

ProcessProxy::~ProcessProxy() {
  for (auto& kv : modules_) {
    auto* module_proxy = kv.first;
    auto& counters = kv.second;
    module_proxy->Remove(counters.data());
  }
  process_.kill();
}

///////////////////////////////////////////////////////////////
// Configuration methods

void ProcessProxy::AddDefaults(Options* options) {
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

void ProcessProxy::Configure(const OptionsPtr& options) { options_ = options; }

zx_status_t ProcessProxy::Connect(InstrumentedProcess instrumented) {
  auto* process = instrumented.mutable_process();
  process_ = std::move(*process);

  auto* eventpair = instrumented.mutable_eventpair();
  eventpair_.Pair(std::move(*eventpair));

  // Create exception channel.
  zx::channel channel;
  auto status = process_.create_exception_channel(0, &channel);
  if (status != ZX_OK) {
    // The process already crashed!
    FX_LOGS(WARNING) << "Failed to create exception channel: " << zx_status_get_string(status);
    result_ = FuzzResult::CRASH;
    return status;
  }

  // If this task produces an error, then the process exited and channel was closed before or during
  // the wait and/or read. |GetResult| will attempt to determine the reason using the exitcode.
  auto task =
      executor_->MakePromiseWaitHandle(zx::unowned_handle(channel.get()), ZX_CHANNEL_READABLE)
          .and_then(
              [this, channel = std::move(channel)](const zx_packet_signal_t& packet) -> ZxResult<> {
                zx_exception_info_t info;
                zx::exception exception;
                auto status = channel.read(0, &info, exception.reset_and_get_address(),
                                           sizeof(info), 1, nullptr, nullptr);
                if (status != ZX_OK) {
                  return fpromise::error(status);
                }
                result_ = FuzzResult::CRASH;
                return fpromise::ok();
              })
          .wrap_with(scope_);
  executor_->schedule_task(std::move(task));

  // Let the process know the proxy is ready to proceed.
  return eventpair_.SignalPeer(0, kSync);
}

zx_status_t ProcessProxy::AddLlvmModule(LlvmModule llvm_module) {
  if (!eventpair_.IsConnected()) {
    FX_LOGS(WARNING) << "Failed to add module: Disconnected.";
    return ZX_ERR_PEER_CLOSED;
  }
  SharedMemory counters;
  auto* inline_8bit_counters = llvm_module.mutable_inline_8bit_counters();
  if (auto status = counters.Link(std::move(*inline_8bit_counters)); status != ZX_OK) {
    FX_LOGS(WARNING) << "Failed to link module: " << zx_status_get_string(status);
    return status;
  }
  auto* module_proxy = pool_->Get(llvm_module.id(), counters.size());
  module_proxy->Add(counters.data(), counters.size());
  modules_[module_proxy] = std::move(counters);
  return eventpair_.SignalPeer(0, kSync);
}

///////////////////////////////////////////////////////////////
// Run-related methods

ZxPromise<> ProcessProxy::Start(bool detect_leaks) {
  return fpromise::make_promise([this, detect_leaks] {
           return AsZxResult(eventpair_.SignalPeer(0, detect_leaks ? kStart : kStartLeakCheck));
         })
      .and_then(eventpair_.WaitFor(kStart))
      .and_then([this](const zx_signals_t& observed) {
        return AsZxResult(eventpair_.SignalSelf(kStart, 0));
      })
      .wrap_with(scope_);
}

zx_status_t ProcessProxy::Finish() { return eventpair_.SignalPeer(0, kFinish); }

Promise<bool, uint64_t> ProcessProxy::AwaitFinish() {
  return eventpair_.WaitFor(kFinish | kFinishWithLeaks)
      .and_then([this](const zx_signals_t& observed) -> ZxResult<bool> {
        auto status = eventpair_.SignalSelf(kFinish | kFinishWithLeaks, 0);
        if (status != ZX_OK) {
          return fpromise::error(status);
        }
        return fpromise::ok(observed == kFinishWithLeaks);
      })
      .or_else([this](const zx_status_t& status) { return fpromise::error(target_id_); })
      .wrap_with(scope_);
}

///////////////////////////////////////////////////////////////
// Status-related methods.

zx_status_t ProcessProxy::GetStats(ProcessStats* out) { return GetStatsForProcess(process_, out); }

ZxPromise<FuzzResult> ProcessProxy::GetResult() {
  FX_DCHECK(options_);
  return fpromise::make_promise([this, awaiting = ZxFuture<int64_t>()](
                                    Context& context) mutable -> ZxResult<FuzzResult> {
           if (result_ != FuzzResult::NO_ERRORS) {
             return fpromise::ok(result_);
           }
           if (!awaiting) {
             awaiting =
                 executor_
                     ->MakePromiseWaitHandle(zx::unowned_handle(process_.get()),
                                             ZX_PROCESS_TERMINATED)
                     .and_then([this](const zx_packet_signal_t& packet) -> ZxResult<int64_t> {
                       FX_DCHECK(packet.observed & ZX_PROCESS_TERMINATED);
                       zx_info_process_t info;
                       auto status = process_.get_info(ZX_INFO_PROCESS, &info, sizeof(info),
                                                       nullptr, nullptr);
                       if (status != ZX_OK) {
                         return fpromise::error(status);
                       }
                       FX_CHECK(info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
                       return fpromise::ok(info.return_code);
                     });
           }
           if (!awaiting(context)) {
             return fpromise::pending();
           }
           if (awaiting.is_error()) {
             return fpromise::error(awaiting.error());
           }
           // Set the result, unless it was already set.
           if (result_ == FuzzResult::NO_ERRORS) {
             auto return_code = awaiting.value();
             if (return_code == options_->malloc_exitcode()) {
               result_ = FuzzResult::BAD_MALLOC;
             } else if (return_code == options_->death_exitcode()) {
               result_ = FuzzResult::DEATH;
             } else if (return_code == options_->leak_exitcode()) {
               result_ = FuzzResult::LEAK;
             } else if (return_code == options_->oom_exitcode()) {
               result_ = FuzzResult::OOM;
             } else if (return_code != 0) {
               result_ = FuzzResult::EXIT;
             }
           }
           return fpromise::ok(result_);
         })
      .wrap_with(scope_);
}

size_t ProcessProxy::Dump(void* buf, size_t size) {
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
