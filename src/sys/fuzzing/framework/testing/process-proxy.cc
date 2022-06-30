// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/process-proxy.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/framework/target/module.h"

namespace fuzzing {

FakeProcessProxy::FakeProcessProxy(ExecutorPtr executor, ModulePoolPtr pool)
    : binding_(this), eventpair_(std::move(executor)), pool_(std::move(pool)) {}

bool FakeProcessProxy::has_module(FakeFrameworkModule* module) const {
  auto id = module->legacy_id();
  auto iter = ids_.find(id[0]);
  return iter != ids_.end() && iter->second == id[1];
}

void FakeProcessProxy::Configure(OptionsPtr options) { options_ = std::move(options); }

fidl::InterfaceRequestHandler<Instrumentation> FakeProcessProxy::GetHandler() {
  return [this](fidl::InterfaceRequest<Instrumentation> request) {
    binding_.Bind(std::move(request));
  };
}

void FakeProcessProxy::FakeProcessProxy::Initialize(InstrumentedProcess instrumented,
                                                    InitializeCallback callback) {
  // The coverage component invokes the callback, but the process waits for the engine's signal.
  callback(CopyOptions(*options_));
  auto* eventpair = instrumented.mutable_eventpair();
  eventpair_.Pair(std::move(*eventpair));
  zx_info_handle_basic_t info;
  auto status =
      instrumented.process().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  process_koid_ = info.koid;
  status = SignalPeer(kSync);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

void FakeProcessProxy::AddLlvmModule(LlvmModule llvm_module, AddLlvmModuleCallback callback) {
  // The coverage component invokes the callback, but the process waits for the engine's signal.
  callback();
  SharedMemory counters;
  auto* inline_8bit_counters = llvm_module.mutable_inline_8bit_counters();
  auto status = counters.Link(std::move(*inline_8bit_counters));
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  auto id = llvm_module.legacy_id();
  ids_[id[0]] = id[1];
  auto* module = pool_->Get(id, counters.size());
  module->Add(counters.data(), counters.size());
  counters_.push_back(std::move(counters));
  status = SignalPeer(kSync);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
}

zx_status_t FakeProcessProxy::SignalPeer(Signal signal) {
  auto status = eventpair_.SignalPeer(0, signal);
  if (completer_) {
    completer_.complete_ok(signal);
  }
  return status;
}

Promise<> FakeProcessProxy::AwaitReceived(Signal signal) {
  return eventpair_.WaitFor(signal)
      .and_then([this](const zx_signals_t& observed) {
        return AsZxResult(eventpair_.SignalSelf(observed, 0));
      })
      .or_else([](const zx_status_t& status) { return fpromise::error(); })
      .wrap_with(scope_);
}

Promise<> FakeProcessProxy::AwaitSent(Signal signal) {
  Bridge<zx_signals_t> bridge;
  completer_ = std::move(bridge.completer);
  return bridge.consumer.promise_or(fpromise::error())
      .and_then([signal](const zx_signals_t& observed) -> Result<> {
        if (signal != observed) {
          return fpromise::error();
        }
        return fpromise::ok();
      });
}

}  // namespace fuzzing
