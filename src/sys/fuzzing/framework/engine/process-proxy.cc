// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/engine/process-proxy.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include <inspector/inspector.h>

#include "src/sys/fuzzing/common/status.h"

namespace fuzzing {

// Public methods.

ProcessProxyImpl::ProcessProxyImpl(std::shared_ptr<ModulePool> pool)
    : binding_(this), pool_(std::move(pool)) {}

ProcessProxyImpl::~ProcessProxyImpl() {
  // Ensure the channel will close by killing the attached process.
  Kill();
  // Deregister this object's shared memory objects from the module pool.
  for (auto& kv : modules_) {
    auto* module_proxy = kv.first;
    auto& counters = kv.second;
    module_proxy->Remove(counters.data());
  }
  exception_channel_.reset();
  if (exception_thread_.joinable()) {
    exception_thread_.join();
  }
}

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

void ProcessProxyImpl::Bind(fidl::InterfaceRequest<ProcessProxy> request,
                            async_dispatcher_t* dispatcher) {
  binding_.set_dispatcher(dispatcher);
  binding_.Bind(std::move(request));
}

void ProcessProxyImpl::Connect(zx::eventpair eventpair, zx::process process,
                               ConnectCallback callback) {
  FX_DCHECK(options_);
  FX_DCHECK(on_signal_);
  FX_DCHECK(on_error_);
  // Wire up signal forwarders.
  coordinator_.Pair(std::move(eventpair), [this](zx_signals_t observed) {
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
        // If the peer exits, invoke the error handler.
        on_error_(this);
        return false;
    }
    on_signal_();
    return true;
  });
  process_ = std::move(process);
  // Start crash handler.
  auto status = process_.create_exception_channel(0, &exception_channel_);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to create exception channel: " << zx_status_get_string(status);
  }
  if (exception_thread_.joinable()) {
    exception_thread_.join();
  }
  exception_thread_ = std::thread([this]() {
    zx_exception_info_t info;
    zx::exception exception;
    if (exception_channel_.wait_one(ZX_CHANNEL_READABLE, zx::time::infinite(), nullptr) != ZX_OK ||
        exception_channel_.read(0, &info, exception.reset_and_get_address(), sizeof(info), 1,
                                nullptr, nullptr) != ZX_OK) {
      // Process exited and channel was closed before or during the wait and/or read.
      return;
    }
    result_ = Result::CRASH;
  });
  // Send options in response.
  auto options = CopyOptions(*options_);
  callback(std::move(options));
}

void ProcessProxyImpl::AddFeedback(Feedback feedback, AddFeedbackCallback callback) {
  if (!feedback.has_id()) {
    FX_LOGS(FATAL) << "Feedback is missing identifier.";
  }
  if (!feedback.has_inline_8bit_counters()) {
    FX_LOGS(FATAL) << "Feedback is missing inline 8-bit counters.";
  }
  SharedMemory counters;
  auto* buffer = feedback.mutable_inline_8bit_counters();
  counters.LinkMirrored(std::move(*buffer));
  auto* module_proxy = pool_->Get(feedback.id(), counters.size());
  module_proxy->Add(counters.data(), counters.size());
  modules_[module_proxy] = std::move(counters);
  callback();
}

void ProcessProxyImpl::Start(bool detect_leaks) {
  leak_suspected_ = false;
  coordinator_.SignalPeer(detect_leaks ? kStartLeakCheck : kStart);
}

void ProcessProxyImpl::Finish() { coordinator_.SignalPeer(kFinish); }

zx_status_t ProcessProxyImpl::GetStats(ProcessStats* out) {
  return GetStatsForProcess(process_, out);
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

void ProcessProxyImpl::Kill() { process_.kill(); }

Result ProcessProxyImpl::Join() {
  FX_DCHECK(options_);
  auto status = process_.wait_one(ZX_PROCESS_TERMINATED, zx::time::infinite(), nullptr);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to wait for process to terminate: " << zx_status_get_string(status);
  }
  zx_info_process_v2_t info;
  status = process_.get_info(ZX_INFO_PROCESS_V2, &info, sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FX_LOGS(FATAL) << "Failed to get info for process: " << zx_status_get_string(status);
  }
  FX_CHECK(info.flags & ZX_INFO_PROCESS_FLAG_EXITED);
  if (result_ != Result::NO_ERRORS) {
    return result_;
  }
  if (info.return_code == options_->malloc_exitcode()) {
    result_ = Result::BAD_MALLOC;
  } else if (info.return_code == options_->death_exitcode()) {
    result_ = Result::DEATH;
  } else if (info.return_code == options_->leak_exitcode()) {
    result_ = Result::LEAK;
  } else if (info.return_code == options_->oom_exitcode()) {
    result_ = Result::OOM;
  } else if (info.return_code != 0) {
    result_ = Result::EXIT;
  }
  return result_;
}

}  // namespace fuzzing
