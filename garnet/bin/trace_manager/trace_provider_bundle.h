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
#include <fuchsia/tracing/provider/cpp/fidl.h>

namespace tracing {

// TODO(PT-127): Both tracelink and tracing.provider providers are handled
// here, until the tracelink renaming is complete, afterwhich this will revert
// back to its original simple self.
struct TraceProviderBundle {
  TraceProviderBundle(fuchsia::tracelink::ProviderPtr tracelink_provider,
                      uint32_t id, zx_koid_t pid, const std::string& name);
  TraceProviderBundle(fuchsia::tracing::provider::ProviderPtr provider,
                      uint32_t id, zx_koid_t pid, const std::string& name);
  ~TraceProviderBundle() = default;

  TraceProviderBundle(const TraceProviderBundle& value) = delete;
  TraceProviderBundle& operator=(const TraceProviderBundle&) = delete;

  TraceProviderBundle(const TraceProviderBundle&& value) = delete;
  TraceProviderBundle& operator=(const TraceProviderBundle&&) = delete;

  // Only one of these is used, depending on |is_tracelink|.
  fuchsia::tracelink::ProviderPtr tracelink_provider;
  fuchsia::tracing::provider::ProviderPtr provider;

  // If true use |tracelink_provider|, otherwise use |provider|.
  bool is_tracelink;

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
