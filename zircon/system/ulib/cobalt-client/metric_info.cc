// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/string_printf.h>

namespace cobalt_client {
namespace internal {
MetricInfo MetricInfo::From(const MetricOptions& options) {
  MetricInfo metric_info;
  metric_info.metric_id = options.metric_id;
  metric_info.component = options.component;
  metric_info.event_codes = options.event_codes;
  return metric_info;
}

bool MetricInfo::operator==(const MetricInfo& rhs) const {
  return rhs.metric_id == metric_id && rhs.event_codes == event_codes && rhs.component == component;
}

bool MetricInfo::operator!=(const MetricInfo& rhs) const { return !(*this == rhs); }

}  // namespace internal
}  // namespace cobalt_client
