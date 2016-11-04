// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "mojo/public/cpp/system/buffer.h"

namespace tracing {

TraceManager::TraceManager() = default;
TraceManager::~TraceManager() = default;

void TraceManager::StartTracing(mojo::Array<mojo::String> categories,
                                mojo::ScopedDataPipeProducerHandle) {
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
    mojo::InterfaceHandle<tracing::TraceProvider> handle,
    const mojo::String& label,
    mojo::Map<mojo::String, mojo::String> categories) {
  auto provider = TraceProviderPtr::Create(handle.Pass());
  trace_providers_.AddInterfacePtr(provider.Pass());
}

}  // namespace tracing
