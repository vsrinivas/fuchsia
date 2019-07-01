// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_
#define GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_

#include <fuchsia/tracing/controller/cpp/fidl.h>
#include <fuchsia/tracing/provider/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_ptr_set.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/sys/cpp/component_context.h>

#include <list>

#include "garnet/bin/trace_manager/config.h"
#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/trace_session.h"

namespace tracing {

namespace controller = ::fuchsia::tracing::controller;
namespace provider = ::fuchsia::tracing::provider;

class TraceManager : public controller::Controller,
                     public provider::Registry {
 public:
  TraceManager(sys::ComponentContext* context, const Config& config);
  ~TraceManager() override;

 private:
  // |Controller| implementation.
  void StartTracing(controller::TraceOptions options,
                    zx::socket output, StartTracingCallback cb) override;
  void StopTracing() override;
  void GetKnownCategories(GetKnownCategoriesCallback callback) override;

  // |TraceRegistry| implementation.
  void RegisterProviderWorker(
      fidl::InterfaceHandle<provider::Provider> provider,
      uint64_t pid, fidl::StringPtr name);
  void RegisterProvider(
      fidl::InterfaceHandle<provider::Provider> provider,
      uint64_t pid, std::string name) override;
  void RegisterProviderSynchronously(
      fidl::InterfaceHandle<provider::Provider> provider,
      uint64_t pid, std::string name,
      RegisterProviderSynchronouslyCallback callback) override;

  void FinalizeTracing();
  void LaunchConfiguredProviders();

  sys::ComponentContext* const context_;
  const Config& config_;

  uint32_t next_provider_id_ = 1u;
  fxl::RefPtr<TraceSession> session_;
  std::list<TraceProviderBundle> providers_;
  // True if tracing has been started, and is not (yet) being stopped.
  bool trace_running_ = false;

  TraceManager(const TraceManager&) = delete;
  TraceManager(TraceManager&&) = delete;
  TraceManager& operator=(const TraceManager&) = delete;
  TraceManager& operator=(TraceManager&&) = delete;
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACE_MANAGER_H_
