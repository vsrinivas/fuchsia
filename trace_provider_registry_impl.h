// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_TRACE_PROVIDER_REGISTRY_IMPL_H_
#define APPS_TRACING_TRACE_PROVIDER_REGISTRY_IMPL_H_

#include <atomic>
#include <chrono>
#include <memory>

#include "apps/tracing/trace_recorder_impl.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/interface_request.h"
#include "mojo/services/tracing/interfaces/trace_provider_registry.mojom.h"

namespace tracing {

class TraceProviderRegistryImpl : public tracing::TraceProviderRegistry,
                                  public tracing::TraceCollector {
 public:
  explicit TraceProviderRegistryImpl(
      const std::chrono::microseconds& grace_period = std::chrono::seconds(5));
  ~TraceProviderRegistryImpl() override;

 private:
  enum class CollectorState { kStarted, kStopped };

  // |TraceCollector| implementation.
  void Start(mojo::ScopedDataPipeProducerHandle stream,
             const mojo::String& categories) override;
  void StopAndFlush() override;

  // |TraceProviderRegistry| implementation.
  void RegisterTraceProvider(
      mojo::InterfaceHandle<tracing::TraceProvider> trace_provider) override;

  std::chrono::microseconds grace_period_;
  CollectorState collector_state_{CollectorState::kStopped};

  mojo::String categories_;
  TraceRecorderImpl trace_recorder_impl_;
  mojo::InterfacePtrSet<TraceProvider> trace_providers_;
  mojo::BindingSet<TraceRecorder> trace_recorder_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceProviderRegistryImpl);
};

}  // namespace tracing

#endif  // APPS_TRACING_TRACE_PROVIDER_REGISTRY_IMPL_H_
