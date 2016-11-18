// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACE_MANAGER_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACE_MANAGER_H_

#include <atomic>
#include <chrono>
#include <list>
#include <memory>
#include <unordered_map>

#include "apps/tracing/services/trace_controller.fidl.h"
#include "apps/tracing/services/trace_registry.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/ftl/macros.h"

namespace tracing {

class TraceManager : public TraceRegistry, public TraceController {
 public:
  TraceManager();
  ~TraceManager() override;

 private:
  enum class ControllerState { kStarted, kStopped };

  struct ProviderInfo {
    explicit ProviderInfo(TraceProviderPtr provider,
                          uint32_t id,
                          std::string label)
        : provider(std::move(provider)), id(id), label(std::move(label)) {}

    TraceProviderPtr provider;
    uint32_t id;
    std::string label;
    mx::vmo current_buffer;
  };

  uint32_t next_provider_id_ = 0u;

  // |TraceController| implementation.
  void StartTracing(fidl::Array<fidl::String> categories,
                    mx::socket output) override;
  void StopTracing() override;
  void GetRegisteredProviders(
      const GetRegisteredProvidersCallback& callback) override;

  // |TraceRegistry| implementation.
  void RegisterTraceProvider(
      fidl::InterfaceHandle<tracing::TraceProvider> provider,
      const fidl::String& label) override;

  void FinalizeTracing();
  bool StartTracingForProvider(ProviderInfo* provider);
  void StopTracingForProvider(ProviderInfo* provider);
  void EraseProvider(TraceProvider* provider);

  ControllerState controller_state_{ControllerState::kStopped};

  fidl::Array<fidl::String> categories_;
  std::list<ProviderInfo> providers_;
  std::vector<typename std::list<ProviderInfo>::iterator> active_providers_;
  mx::socket output_;
  int generation_ = 0;

  FTL_DISALLOW_COPY_AND_ASSIGN(TraceManager);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACE_MANAGER_H_
