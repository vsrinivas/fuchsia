// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACEE_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACEE_H_

#include <functional>

#include <mx/socket.h>
#include <mx/vmo.h>

#include "apps/tracing/src/trace_manager/trace_provider_bundle.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/string.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace tracing {

class Tracee {
 public:
  explicit Tracee(TraceProviderBundle* bundle);

  bool operator==(TraceProviderBundle* bundle) const;

  bool Start(size_t buffer_size, fidl::Array<fidl::String> categories);
  void Stop(ftl::Closure stop_callback);
  bool WriteRecords(mx::socket& socket) const;

  TraceProviderBundle* bundle() const { return bundle_; }

 private:
  bool WriteProviderInfoRecord(mx::socket& socket) const;

  TraceProviderBundle* bundle_;
  mx::vmo vmo_;
  size_t vmo_size_ = 0u;

  FTL_DISALLOW_COPY_AND_ASSIGN(Tracee);
};

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACEE_H_
