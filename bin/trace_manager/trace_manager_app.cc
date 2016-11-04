// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <mojo/system/main.h>

#include <utility>

#include "apps/tracing/src/trace_manager/trace_manager.h"
#include "lib/ftl/macros.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/binding_set.h"

namespace tracing {

class TraceManagerApp : public mojo::ApplicationImplBase {
 public:
  TraceManagerApp() = default;
  ~TraceManagerApp() override = default;

 private:
  // |ApplicationImplBase| override:
  bool OnAcceptConnection(
      mojo::ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<TraceRegistry>([this](
        const mojo::ConnectionContext& connection_context,
        mojo::InterfaceRequest<TraceRegistry> request) {
      trace_registry_bindings_.AddBinding(&trace_manager_, std::move(request));
    });

    service_provider_impl->AddService<TraceController>(
        [this](const mojo::ConnectionContext& connection_context,
               mojo::InterfaceRequest<TraceController> request) {
          trace_controller_bindings_.AddBinding(&trace_manager_,
                                                std::move(request));
        });
    return true;
  }

  TraceManager trace_manager_;
  mojo::BindingSet<TraceRegistry> trace_registry_bindings_;
  mojo::BindingSet<TraceController> trace_controller_bindings_;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceManagerApp);
};

}  // namespace tracing

MojoResult MojoMain(MojoHandle request) {
  tracing::TraceManagerApp trace_manager_app;
  return mojo::RunApplication(request, &trace_manager_app);
}
