// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TRACING_SRC_TRACE_MANAGER_TRACE_PROVIDER_BUNDLE_H_
#define APPS_TRACING_SRC_TRACE_MANAGER_TRACE_PROVIDER_BUNDLE_H_

#include <stdint.h>

#include <iosfwd>
#include <string>

#include "lib/tracing/fidl/trace_provider.fidl.h"

namespace tracing {

struct TraceProviderBundle {
  TraceProviderPtr provider;
  uint32_t id;
  std::string label;
};

std::ostream& operator<<(std::ostream& out, const TraceProviderBundle& bundle);

}  // namespace tracing

#endif  // APPS_TRACING_SRC_TRACE_MANAGER_TRACE_PROVIDER_BUNDLE_H_
