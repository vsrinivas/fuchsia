// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/process-proxy.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/framework/target/module.h"

namespace fuzzing {

FakeProcessProxy::FakeProcessProxy(const std::shared_ptr<ModulePool>& pool)
    : binding_(this), pool_(std::move(pool)) {}

bool FakeProcessProxy::has_module(FakeFrameworkModule* module) const {
  auto id = module->id();
  auto iter = ids_.find(id[0]);
  return iter != ids_.end() && iter->second == id[1];
}

void FakeProcessProxy::Configure(const std::shared_ptr<Options>& options) { options_ = options; }

InstrumentationSyncPtr FakeProcessProxy::Bind(bool disable_warnings) {
  if (disable_warnings) {
    options_->set_purge_interval(0);
    options_->set_malloc_limit(0);
  }
  InstrumentationSyncPtr instrumentation;
  auto request = instrumentation.NewRequest();
  binding_.Bind(std::move(request));
  return instrumentation;
}

void FakeProcessProxy::Initialize(InstrumentedProcess instrumented, InitializeCallback callback) {
  auto* eventpair = instrumented.mutable_eventpair();
  coordinator_.Pair(std::move(*eventpair));
  zx_info_handle_basic_t info;
  auto status =
      instrumented.process().get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  process_koid_ = info.koid;
  callback(CopyOptions(*options_));
  coordinator_.SignalPeer(kSync);
}

void FakeProcessProxy::AddLlvmModule(LlvmModule llvm_module, AddLlvmModuleCallback callback) {
  SharedMemory counters;
  auto* inline_8bit_counters = llvm_module.mutable_inline_8bit_counters();
  counters.LinkMirrored(std::move(*inline_8bit_counters));
  auto id = llvm_module.id();
  ids_[id[0]] = id[1];
  auto* module = pool_->Get(id, counters.size());
  module->Add(counters.data(), counters.size());
  counters_.push_back(std::move(counters));
  callback();
  coordinator_.SignalPeer(kSync);
}

}  // namespace fuzzing
