// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/src/trace_manager/trace_manager.h"

namespace tracing {

TraceManager::TraceManager() = default;
TraceManager::~TraceManager() = default;

void TraceManager::StartTracing(fidl::Array<fidl::String> categories,
                                mx::datapipe_producer output) {
  if (controller_state_ == ControllerState::kStarted)
    return;

  controller_state_ = ControllerState::kStarted;
  categories_ = std::move(categories);
}

void TraceManager::StopTracing() {
  if (controller_state_ == ControllerState::kStopped)
    return;

  controller_state_ = ControllerState::kStarted;
}

void TraceManager::RegisterTraceProvider(
    fidl::InterfaceHandle<TraceProvider> handle,
    const fidl::String& label,
    fidl::Map<fidl::String, fidl::String> categories) {
  auto provider = TraceProviderPtr::Create(std::move(handle));
  trace_providers_.AddInterfacePtr(std::move(provider));
}

}  // namespace tracing
