// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_PROJECT_CONFIG_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_PROJECT_CONFIG_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "garnet/bin/metric_broker/config/config-registry.h"
#include "metric_config.h"
#include "types.h"

namespace broker_service::cobalt {

// Represents a collection of |MetricConfigs| and provide |metric_id| based look up.
class ProjectConfig {
 public:
  ProjectConfig() = delete;
  ProjectConfig(std::string_view project_name, uint64_t update_interval_sec)
      : project_name_(project_name), update_interval_sec_(update_interval_sec) {
    configs_.reserve(10);
  }
  ProjectConfig(const ProjectConfig&) = delete;
  ProjectConfig(ProjectConfig&&) = default;
  ProjectConfig& operator=(const ProjectConfig&) = delete;
  ProjectConfig& operator=(ProjectConfig&&) = delete;
  ~ProjectConfig() = default;

  // Returns the name of the cobalt project associated with this config.
  [[nodiscard]] std::string_view project_name() const { return project_name_; }

  // Returns the number of seconds to wait between each update sent to |CobaltService|.
  [[nodiscard]] uint64_t update_interval_sec() const { return update_interval_sec_; }

  // Returns an existing |MetricConfig| mapped to this |metric_id|, if any.
  std::optional<MetricConfig*> Find(uint64_t metric_id);

  // Returns an existing |MetricConfig| mapped to this |metric_id| with a given |type|.
  // Returns |nullopt| if a mapping exists but with a different type.
  std::optional<MetricConfig*> FindOrCreate(uint64_t metric_id, SupportedType type);

  // Returns true if the |project_| contains no metric configs.
  bool IsEmpty() const { return metric_to_index_.empty(); }

  // Clears all existing configs.
  void Clear();

  // Const-iterators for existing |MetricConfigs|.
  [[nodiscard]] auto begin() const { return configs_.begin(); }
  [[nodiscard]] auto end() const { return configs_.end(); }

 private:
  // Mapping of each Metric Id to the index in the config vector.
  std::unordered_map<uint64_t, uint64_t> metric_to_index_;

  // Collections of configurations for each metric.
  std::vector<MetricConfig> configs_;

  // Cobalt project name.
  std::string project_name_;

  // How often should the latest snapshot be pushed to cobalt service.
  uint64_t update_interval_sec_;
};

// Alias for registering Cobalt related |ProjectConfigs|.
using ConfigRegistry =
    broker_service::Registry<broker_service::SupportedService::kCobaltService, ProjectConfig>;

}  // namespace broker_service::cobalt

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_PROJECT_CONFIG_H_
