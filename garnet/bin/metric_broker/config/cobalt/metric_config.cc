// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/metric_broker/config/cobalt/metric_config.h"

#include <utility>

namespace broker_service::cobalt {

std::optional<EventCodes> MetricConfig::GetEventCodes(std::string_view metric_path) const {
  auto it = code_mapping_.find(metric_path);
  if (it == end()) {
    return std::nullopt;
  }
  return it->second;
}

void MetricConfig::InsertOrUpdate(std::string_view metric_path, const EventCodes& code) {
  code_mapping_.insert(std::make_pair(metric_path, code));
}

void MetricConfig::Clear() { code_mapping_.clear(); }

}  // namespace broker_service::cobalt
