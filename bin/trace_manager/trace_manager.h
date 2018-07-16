// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_
#define GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_

#include <list>

#include <fuchsia/tracelink/cpp/fidl.h>
#include <fuchsia/tracing/cpp/fidl.h>

#include "garnet/bin/trace_manager/config.h"
#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/trace_session.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"

namespace tracing {

class TraceManager : public fuchsia::tracelink::Registry,
                     public fuchsia::tracing::TraceController {
 public:
  TraceManager(component::StartupContext* context, const Config& config);
  ~TraceManager() override;

 private:
  // |TraceController| implementation.
  void StartTracing(fuchsia::tracing::TraceOptions options, zx::socket output,
                    StartTracingCallback cb) override;
  void StopTracing() override;
  void GetKnownCategories(GetKnownCategoriesCallback callback) override;

  // |TraceRegistry| implementation.
  void RegisterTraceProvider(
      fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider) override;

  void FinalizeTracing();
  void LaunchConfiguredProviders();

  component::StartupContext* const context_;
  const Config& config_;

  uint32_t next_provider_id_ = 1u;
  fxl::RefPtr<TraceSession> session_;
  std::list<TraceProviderBundle> providers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceManager);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_
