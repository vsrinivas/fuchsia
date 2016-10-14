// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include "apps/tracing/trace_provider_registry_impl.h"
#include "mojo/public/cpp/utility/run_loop.h"

namespace tracing {

TraceProviderRegistryImpl::TraceProviderRegistryImpl(
    const std::chrono::microseconds& grace_period)
    : grace_period_(grace_period) {}

TraceProviderRegistryImpl::~TraceProviderRegistryImpl() = default;

void TraceProviderRegistryImpl::Start(mojo::ScopedDataPipeProducerHandle stream,
                                      const mojo::String& categories) {
  if (collector_state_ == CollectorState::kStarted)
    return;

  collector_state_ = CollectorState::kStarted;

  categories_ = categories;
  trace_recorder_impl_.Start(std::move(stream));
  trace_providers_.ForAllPtrs([this](TraceProvider* provider) {
    provider->StartTracing(
        categories_,
        trace_recorder_bindings_.AddBinding(&trace_recorder_impl_).Pass());
  });
}

void TraceProviderRegistryImpl::StopAndFlush() {
  if (collector_state_ == CollectorState::kStopped)
    return;

  collector_state_ = CollectorState::kStarted;
  categories_.reset();
  trace_providers_.ForAllPtrs(
      [](TraceProvider* provider) { provider->StopTracing(); });

  mojo::RunLoop::current()->PostDelayedTask(
      [this]() {
        trace_recorder_bindings_.CloseAllBindings();
        trace_recorder_impl_.Stop();
      },
      grace_period_.count());
}

void TraceProviderRegistryImpl::RegisterTraceProvider(
    mojo::InterfaceHandle<tracing::TraceProvider> handle) {
  auto provider = TraceProviderPtr::Create(handle.Pass());
  // We start the provider if tracing has been started previously.
  // This supports the use-case of enabling tracing and, after that,
  // starting the applications that should be traced.
  if (collector_state_ == CollectorState::kStarted)
    provider->StartTracing(
        categories_,
        trace_recorder_bindings_.AddBinding(&trace_recorder_impl_).Pass());

  trace_providers_.AddInterfacePtr(provider.Pass());
}

}  // namespace tracing
