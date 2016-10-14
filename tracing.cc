// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <utility>

#include "apps/tracing/trace_provider_registry_impl.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"
#include "mojo/services/tracing/interfaces/trace_provider_registry.mojom.h"
#include "mojo/services/tracing/interfaces/tracing.mojom.h"

namespace tracing {

// Provides an implementation of mojo.tracing.TraceProviderRegistry and
// mojo.tracing.TraceCollector.
class TracingApp : public mojo::ApplicationImplBase {
 public:
  TracingApp() = default;
  ~TracingApp() override = default;

 private:
  // |ApplicationImplBase| override:
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<tracing::TraceProviderRegistry>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<tracing::TraceProviderRegistry> request) {
          trace_provider_registry_bindings_.AddBinding(
              &trace_provider_registry_, std::move(request));
        });

    service_provider_impl->AddService<tracing::TraceCollector>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<tracing::TraceCollector> request) {
          trace_collector_bindings_.AddBinding(&trace_provider_registry_,
                                               std::move(request));
        });
    return true;
  }

  TraceProviderRegistryImpl trace_provider_registry_;
  mojo::BindingSet<TraceProviderRegistry> trace_provider_registry_bindings_;
  mojo::BindingSet<TraceCollector> trace_collector_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TracingApp);
};

}  // namespace tracing

MojoResult MojoMain(MojoHandle request) {
  tracing::TracingApp tracing_app;
  return mojo::RunApplication(request, &tracing_app);
}
