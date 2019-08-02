// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "project_config.h"

namespace broker_service::cobalt {

std::optional<MetricConfig*> ProjectConfig::Find(uint64_t metric_id) {
  auto it = metric_to_index_.find(metric_id);
  if (it == metric_to_index_.end()) {
    return std::nullopt;
  }
  return &configs_[it->second];
}

std::optional<MetricConfig*> ProjectConfig::FindOrCreate(uint64_t metric_id, SupportedType type) {
  auto config = Find(metric_id);

  if (config.has_value()) {
    if (config.value()->type() != type) {
      return std::nullopt;
    }
    return config.value();
  }
  configs_.emplace_back(metric_id, type);
  metric_to_index_[metric_id] = configs_.size() - 1;
  return &configs_.back();
}

void ProjectConfig::Clear() {
  configs_.clear();
  metric_to_index_.clear();
}

}  // namespace broker_service::cobalt
