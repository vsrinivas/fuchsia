// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// *** PT-127 ****************************************************************
// This file is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

#include "garnet/bin/trace_manager/tracelink_manager.h"

#include <lib/zx/time.h>

#include <algorithm>
#include <iostream>

#include "fuchsia/sys/cpp/fidl.h"
#include "lib/fidl/cpp/clone.h"

namespace tracing {

TracelinkManager::TracelinkManager(TraceManager* trace_manager)
    : trace_manager_(trace_manager) {
}

TracelinkManager::~TracelinkManager() = default;

void TracelinkManager::RegisterTraceProviderWorker(
    fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider, uint64_t pid,
    fidl::StringPtr name) {
  FXL_VLOG(2) << "Registering tracelink provider {" << pid << ":" << name.get() << "}";
  auto it = trace_manager_->providers_.emplace(
      trace_manager_->providers_.end(),
      provider.Bind(), trace_manager_->next_provider_id_++, pid, name.get());

  it->tracelink_provider.set_error_handler([this, it](zx_status_t status) {
    if (trace_manager_->session_)
      trace_manager_->session_->RemoveDeadProvider(&(*it));
    trace_manager_->providers_.erase(it);
  });

  if (trace_manager_->session_)
    trace_manager_->session_->AddProvider(&(*it));
}

void TracelinkManager::RegisterTraceProviderDeprecated(
    fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider) {
  RegisterTraceProviderWorker(std::move(provider), ZX_KOID_INVALID,
                              fidl::StringPtr(""));
}

void TracelinkManager::RegisterTraceProvider(
    fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider, uint64_t pid,
    std::string name) {
  RegisterTraceProviderWorker(std::move(provider), pid, std::move(name));
}

void TracelinkManager::RegisterTraceProviderSynchronously(
    fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider, uint64_t pid,
    std::string name, RegisterTraceProviderSynchronouslyCallback callback) {
  RegisterTraceProviderWorker(std::move(provider), pid, std::move(name));
  callback(ZX_OK, trace_manager_->trace_running_);
}

}  // namespace tracing
