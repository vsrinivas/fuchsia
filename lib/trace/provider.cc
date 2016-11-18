// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace/provider.h"

#include "apps/tracing/lib/trace/writer.h"
#include "apps/tracing/services/trace_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/vmo/shared_vmo.h"

namespace tracing {
namespace {

class TraceProviderImpl : public TraceProvider {
 public:
  // Initializes a new instance, registering itself with |registry|.
  TraceProviderImpl(TraceRegistryPtr registry, const TraceSettings& settings)
      : state_(State::kStopped),
        binding_(this),
        registry_(std::move(registry)) {
    TraceProviderPtr provider;
    fidl::InterfaceRequest<TraceProvider> provider_request =
        fidl::GetProxy(&provider);

    binding_.Bind(std::move(provider_request));
    registry_->RegisterTraceProvider(
        std::move(provider), fidl::String::From(settings.provider_label));
  }

 private:
  // |TraceProvider| implementation
  //
  // Preprocesses |categories| and builds a lookup-table
  // for fast checks if a trace event should be traced or not.
  void Start(mx::vmo initial_buffer,
             mx::vmo next_buffer,
             ::fidl::Array<::fidl::String> categories) override {
    FTL_VLOG(2) << "TraceProvider::Start called";

    if (state_ != State::kStopped)
      return;

    state_ = State::kStarted;
    writer::StartTracing(std::move(initial_buffer), mx::vmo(),
                         categories.To<std::vector<std::string>>());
  }

  void Stop(const StopCallback& cb) override {
    FTL_VLOG(2) << "TraceProvider::Stop called";

    if (state_ == State::kStarted) {
      state_ = State::kStopped;
      writer::StopTracing();
    }

    cb();
  }

  void RecycleBuffer() override {}

  enum class State { kStarted, kStopped };
  State state_;
  fidl::Binding<TraceProvider> binding_;
  TraceRegistryPtr registry_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TraceProviderImpl);
};

// gTracer is the singleton |Tracer| instance.
TraceProviderImpl* g_tracer = nullptr;

}  // namespace

bool InitializeTracerFromCommandLine(modular::ApplicationContext* app_context,
                                     const ftl::CommandLine& command_line) {
  TraceSettings settings;
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

}  // namespace tracing
