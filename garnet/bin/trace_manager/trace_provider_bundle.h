// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_TRACE_MANAGER_TRACE_PROVIDER_BUNDLE_H_
#define GARNET_BIN_TRACE_MANAGER_TRACE_PROVIDER_BUNDLE_H_

#include <stdint.h>

#include <iosfwd>
#include <map>
#include <string>

#include <fuchsia/tracelink/cpp/fidl.h>

namespace tracing {

struct TraceProviderBundle {
  fuchsia::tracelink::ProviderPtr provider;
  uint32_t id;
  zx_koid_t pid;
  const std::string name;
};

struct TraceProviderSpec {
  uint32_t buffer_size_megabytes;
};

using TraceProviderSpecMap = std::map<std::string, TraceProviderSpec>;

std::ostream& operator<<(std::ostream& out, const TraceProviderBundle& bundle);

}  // namespace tracing

#endif  // GARNET_BIN_TRACE_MANAGER_TRACE_PROVIDER_BUNDLE_H_
