// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cobalt-client/cpp/metric-options.h>
#include <cobalt-client/cpp/types-internal.h>
#include <fbl/string_printf.h>

namespace cobalt_client {
namespace internal {

LocalMetricInfo LocalMetricInfo::From(const MetricOptions& options) {
    LocalMetricInfo metric_info;
    if (!options.name.empty()) {
        metric_info.name = options.name;
        return metric_info;
    }

    if (options.get_metric_name != nullptr) {
        metric_info.name = options.get_metric_name(options.metric_id);
    } else {
        metric_info.name = fbl::StringPrintf("%u", options.metric_id);
    }

    if (!options.component.empty()) {
        metric_info.name =
            fbl::StringPrintf("%s.%s", metric_info.name.c_str(), options.component.c_str());
    }

    if (options.get_event_name != nullptr) {
        metric_info.name = fbl::StringPrintf("%s.%s", metric_info.name.c_str(),
                                             options.get_event_name(options.event_code));
    } else {
        metric_info.name = fbl::StringPrintf("%s.%u", metric_info.name.c_str(), options.event_code);
    }

    return metric_info;
}

bool LocalMetricInfo::operator==(const LocalMetricInfo& rhs) const {
    return rhs.name == name;
}

bool LocalMetricInfo::operator!=(const LocalMetricInfo& rhs) const {
    return !(*this == rhs);
}

RemoteMetricInfo RemoteMetricInfo::From(const MetricOptions& options) {
    RemoteMetricInfo metric_info;
    metric_info.metric_id = options.metric_id;
    metric_info.component = options.component;
    metric_info.event_code = options.event_code;
    return metric_info;
}

bool RemoteMetricInfo::operator==(const RemoteMetricInfo& rhs) const {
    return rhs.metric_id == metric_id && rhs.event_code == event_code && rhs.component == component;
}

bool RemoteMetricInfo::operator!=(const RemoteMetricInfo& rhs) const {
    return !(*this == rhs);
}

} // namespace internal
} // namespace cobalt_client
