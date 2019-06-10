// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// *** PT-127 ****************************************************************
// This file is temporary, and provides a sufficient API to exercise
// the old fuchsia.tracelink FIDL API. It will go away once all providers have
// updated to use the new fuchsia.tracing.provider FIDL API (which is
// different from fuchsia.tracelink in name only).
// ***************************************************************************

#ifndef GARNET_BIN_TRACE_MANAGER_TRACELINK_MANAGER_H_
#define GARNET_BIN_TRACE_MANAGER_TRACELINK_MANAGER_H_

#include <fuchsia/tracelink/cpp/fidl.h>

#include <list>

#include "garnet/bin/trace_manager/config.h"
#include "garnet/bin/trace_manager/trace_manager.h"
#include "garnet/bin/trace_manager/trace_provider_bundle.h"
#include "garnet/bin/trace_manager/trace_session.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "src/lib/fxl/macros.h"

namespace tracing {

class TracelinkManager : public fuchsia::tracelink::Registry {
 public:
  TracelinkManager(TraceManager* trace_manager);
  ~TracelinkManager() override;

  void RegisterTraceProviderWorker(
      fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider,
      uint64_t pid, fidl::StringPtr name);
  void RegisterTraceProviderDeprecated(
      fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider) override;
  void RegisterTraceProvider(
      fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider,
      uint64_t pid, std::string name) override;
  void RegisterTraceProviderSynchronously(
      fidl::InterfaceHandle<fuchsia::tracelink::Provider> provider,
      uint64_t pid, std::string name,
      RegisterTraceProviderSynchronouslyCallback callback) override;

  TraceManager* const trace_manager_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TracelinkManager);
};

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACELINK_MANAGER_H_
