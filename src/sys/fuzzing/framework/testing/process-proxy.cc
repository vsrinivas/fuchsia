// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/framework/testing/process-proxy.h"

#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

#include "src/sys/fuzzing/framework/target/module.h"

namespace fuzzing {

FakeProcessProxy::FakeProcessProxy(std::shared_ptr<ModulePool> pool)
    : binding_(this), pool_(std::move(pool)) {}

bool FakeProcessProxy::has_module(FakeModule* module) const {
  auto id = module->id();
  auto iter = ids_.find(id[0]);
  return iter != ids_.end() && iter->second == id[1];
}

void FakeProcessProxy::Configure(const std::shared_ptr<Options>& options) { options_ = options; }

ProcessProxySyncPtr FakeProcessProxy::Bind(async_dispatcher_t* dispatcher, bool disable_warnings) {
  if (disable_warnings) {
    options_->set_purge_interval(0);
    options_->set_malloc_limit(0);
  }
  ProcessProxySyncPtr proxy;
  auto request = proxy.NewRequest();
  binding_.set_dispatcher(dispatcher);
  binding_.Bind(std::move(request));
  return proxy;
}

void FakeProcessProxy::Connect(zx::eventpair eventpair, zx::process process,
                               ConnectCallback callback) {
  coordinator_.Pair(std::move(eventpair));
  zx_info_handle_basic_t info;
  auto status = process.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
  FX_DCHECK(status == ZX_OK) << zx_status_get_string(status);
  process_koid_ = info.koid;
  callback(CopyOptions(*options_));
}

void FakeProcessProxy::AddFeedback(Feedback feedback, AddFeedbackCallback callback) {
  SharedMemory counters;
  auto* buffer = feedback.mutable_inline_8bit_counters();
  counters.LinkMirrored(std::move(*buffer));
  auto id = feedback.id();
  ids_[id[0]] = id[1];
  auto* module = pool_->Get(id, counters.size());
  module->Add(counters.data(), counters.size());
  counters_.push_back(std::move(counters));
  callback();
}

}  // namespace fuzzing
