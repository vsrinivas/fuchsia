// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/tracing/lib/trace_event/trace_provider.h"

#include "apps/tracing/lib/trace_event/internal/trace_writer.h"
#include "apps/tracing/services/trace_manager.mojom.h"
#include "apps/tracing/services/trace_provider.mojom.h"
#include "lib/ftl/logging.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/bindings/interface_handle.h"

namespace tracing {
namespace {

constexpr size_t kTraceBufferSize = 3 * 1024 * 1024;
// We allocate three MB of memory for tracing.
char* g_memory = static_cast<char*>(malloc(kTraceBufferSize));

class TraceProviderImpl : public TraceProvider {
 public:
  // Initializes a new instance, registering itself with |registry|.
  TraceProviderImpl(TraceRegistryPtr registry)
      : state_(State::kStopped), binding_(this) {
    TraceProviderPtr provider;
    mojo::InterfaceRequest<TraceProvider> provider_request =
        mojo::GetProxy(&provider);

    binding_.Bind(provider_request.Pass());
    registry->RegisterTraceProvider(
        provider.Pass(), mojo::String::From(label_),
        mojo::Map<mojo::String, mojo::String>::From(known_categories_));
  }

 private:
  // |TraceProvider| implementation
  //
  // Preprocesses |categories| and builds a lookup-table
  // for fast checks if a trace event should be traced or not.
  void Start(mojo::ScopedSharedBufferHandle initial_buffer,
             mojo::ScopedSharedBufferHandle next_buffer,
             mojo::Array<mojo::String> categories) override {
    if (state_ != State::kStopped)
      return;

    state_ = State::kStarted;

    // TODO(tvoss): Once we pass in a VMO here, the Writer API
    // should be initialized to the memory handled by the VMO.
    internal::StartTracing(g_memory, kTraceBufferSize,
                           categories.To<std::vector<std::string>>());
  }

  void Stop(const StopCallback& cb) override {
    if (state_ != State::kStarted)
      return;

    state_ = State::kStopped;
    internal::StopTracing();

    cb.Run();
  }

  void RecycleBuffer() override {}

  enum class State { kStarted, kStopped };
  State state_;
  mojo::Binding<TraceProvider> binding_;
  std::string label_ = "HeWhoShallNotBeNamed";
  std::map<std::string, std::string> known_categories_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TraceProviderImpl);
};

// Url of the system default trace provider registry service.
constexpr const char* kDefaultRegistryUrl = "mojo:trace_manager";

// gTracer is the singleton |Tracer| instance.
TraceProviderImpl* g_tracer = nullptr;

}  // namespace

void InitializeTracer(::mojo::ApplicationConnector* app_connector) {
  TraceRegistryPtr registry;
  mojo::ConnectToService(app_connector, kDefaultRegistryUrl,
                         mojo::GetProxy(&registry));
  InitializeTracer(std::move(registry));
}

void InitializeTracer(::tracing::TraceRegistryPtr registry) {
  FTL_CHECK(!g_tracer) << "Tracer is already initialized.";

  if (g_tracer)
    return;

  g_tracer = new TraceProviderImpl(std::move(registry));
}

void DestroyTracer() {
  delete g_tracer;
  g_tracer = nullptr;
}

}  // namespace tracing
