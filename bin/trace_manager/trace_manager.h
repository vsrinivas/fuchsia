// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACE_MANAGER_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACE_MANAGER_H_

#include <list>

#include "lib/app/cpp/application_context.h"
#include "apps/tracing/services/trace_controller.fidl.h"
#include "apps/tracing/services/trace_registry.fidl.h"
#include "apps/tracing/src/trace_manager/config.h"
#include "apps/tracing/src/trace_manager/trace_provider_bundle.h"
#include "apps/tracing/src/trace_manager/trace_session.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/one_shot_timer.h"

namespace tracing {

class TraceManager : public TraceRegistry, public TraceController {
 public:
  TraceManager(app::ApplicationContext* context, const Config& config);
  ~TraceManager() override;

 private:
  // |TraceController| implementation.
  void StartTracing(TraceOptionsPtr options,
                    mx::socket output,
                    const StartTracingCallback& cb) override;
  void StopTracing() override;
  void DumpProvider(uint32_t provider_id, mx::socket output) override;
  void GetKnownCategories(const GetKnownCategoriesCallback& callback) override;
  void GetRegisteredProviders(
      const GetRegisteredProvidersCallback& callback) override;

  // |TraceRegistry| implementation.
  void RegisterTraceProvider(
      fidl::InterfaceHandle<tracing::TraceProvider> provider,
      const fidl::String& label) override;

  void FinalizeTracing();
  void LaunchConfiguredProviders();

  app::ApplicationContext* const context_;
  const Config& config_;

  uint32_t next_provider_id_ = 1u;
  fxl::RefPtr<TraceSession> session_;
  fxl::OneShotTimer session_finalize_timeout_;
  std::list<TraceProviderBundle> providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceManager);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACE_MANAGER_H_
