// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/provider.h"

#include "apps/tracing/lib/trace/internal/trace_provider_impl.h"
#include "lib/ftl/logging.h"

using namespace tracing::internal;

namespace tracing {
namespace {

// The singleton |TraceProviderImpl| instance.
TraceProviderImpl* g_tracer = nullptr;

}  // namespace

bool InitializeTracerFromCommandLine(modular::ApplicationContext* app_context,
                                     const ftl::CommandLine& command_line,
                                     const TraceSettings default_settings) {
  TraceSettings settings = default_settings;
  if (!ParseTraceSettings(command_line, &settings))
    return false;
  InitializeTracer(app_context, settings);
  return true;
}

void InitializeTracer(modular::ApplicationContext* app_context,
                      const TraceSettings& settings) {
  auto registry = app_context->ConnectToEnvironmentService<TraceRegistry>();
  InitializeTracer(std::move(registry), settings);
}

void InitializeTracer(TraceRegistryPtr registry,
                      const TraceSettings& settings) {
  FTL_CHECK(!g_tracer) << "Tracer is already initialized.";
  g_tracer = new TraceProviderImpl(std::move(registry), settings);
}

void DestroyTracer() {
  delete g_tracer;
  g_tracer = nullptr;
}

void SetDumpCallback(DumpCallback callback) {
  FTL_CHECK(g_tracer)
      << "Trace must be initialized before setting dump callback";
  g_tracer->SetDumpCallback(std::move(callback));
}

}  // namespace tracing
