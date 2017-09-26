// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_TRACE_INTERNAL_TRACE_PROVIDER_IMPL_H_
#define GARNET_LIB_TRACE_INTERNAL_TRACE_PROVIDER_IMPL_H_

#include <memory>
#include <string>
#include <vector>

#include <zx/eventpair.h>
#include <zx/socket.h>
#include <zx/vmo.h>

#include "garnet/lib/trace/provider.h"
#include "garnet/lib/trace/settings.h"
#include "lib/tracing/fidl/trace_provider.fidl.h"
#include "lib/tracing/fidl/trace_registry.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/weak_ptr.h"

namespace tracing {
namespace internal {

class TraceProviderImpl : public TraceProvider {
 public:
  TraceProviderImpl(TraceRegistryPtr registry, const TraceSettings& settings);
  ~TraceProviderImpl() override;

  void SetDumpCallback(DumpCallback callback);

 private:
  // |TraceProvider|
  void Start(zx::vmo buffer,
             zx::eventpair fence,
             ::fidl::Array<::fidl::String> categories,
             const StartCallback& callback) override;
  void Stop() override;
  void Dump(zx::socket output) override;

  void StartPendingTrace();
  void FinishedTrace();

  TraceRegistryPtr registry_;
  fidl::Binding<TraceProvider> binding_;

  enum class State { kStarted, kStopping, kStopped };
  State state_ = State::kStopped;

  DumpCallback dump_callback_;

  struct PendingTrace {
    zx::vmo buffer;
    zx::eventpair fence;
    std::vector<std::string> enabled_categories;
    StartCallback start_callback;
  };
  std::unique_ptr<PendingTrace> pending_trace_;

  fxl::WeakPtrFactory<TraceProviderImpl> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TraceProviderImpl);
};

}  // namespace internal
}  // namespace tracing

#endif  // GARNET_LIB_TRACE_INTERNAL_TRACE_PROVIDER_IMPL_H_
