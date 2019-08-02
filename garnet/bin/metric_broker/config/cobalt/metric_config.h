// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_METRIC_CONFIG_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_METRIC_CONFIG_H_

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"
#include "garnet/bin/metric_broker/config/cobalt/types.h"

namespace broker_service::cobalt {

// Inspect metric at the given paths, are of |MetricConfig::type()| and are mapped to a cobalt
// metric with |MetricConfig::metric_id()|.
class MetricConfig {
 public:
  MetricConfig(uint64_t metric_id, SupportedType type) : metric_id_(metric_id), type_(type) {}
  MetricConfig(const MetricConfig&) = delete;
  MetricConfig(MetricConfig&&) = default;
  MetricConfig& operator=(const MetricConfig) = delete;
  MetricConfig& operator=(MetricConfig&&) = default;
  ~MetricConfig() = default;

  // Returns the event codes mapped |metric_path|, if any.
  [[nodiscard]] std::optional<EventCodes> GetEventCodes(std::string_view metric_path) const;

  // Inserts or Updates a mapping from |metric_path| to |codes|.
  void InsertOrUpdate(std::string_view metric_path, const EventCodes& code);

  // Clears all existing mappings for this metric.
  void Clear();

  // Returns the cobalt metric id for this configuration.
  [[nodiscard]] uint64_t metric_id() const { return metric_id_; }

  // Returns the type of this metric.
  [[nodiscard]] SupportedType type() const { return type_; }

  // Returns true if |this| contains no mapped paths.
  [[nodiscard]] bool IsEmpty() const { return code_mapping_.empty(); }

  // Const iterators for the existing mappings.
  [[nodiscard]] auto begin() const { return code_mapping_.begin(); }
  [[nodiscard]] auto end() const { return code_mapping_.end(); }

 private:
  // Maps |path| to a set of event codes.
  std::map<std::string, EventCodes, std::less<>> code_mapping_;

  // Cobalt metric id expected in the backend.
  uint64_t metric_id_;

  // Expected metric type at the given paths.
  SupportedType type_;
};

}  // namespace broker_service::cobalt

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_METRIC_CONFIG_H_
