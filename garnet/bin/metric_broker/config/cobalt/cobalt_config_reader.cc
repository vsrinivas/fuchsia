// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cobalt_config_reader.h"

#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"
#include "garnet/bin/metric_broker/config/cobalt/metric_config.h"
#include "garnet/bin/metric_broker/config/cobalt/project_config.h"
#include "garnet/bin/metric_broker/config/cobalt/types.h"
#include "rapidjson/document.h"
#include "rapidjson/schema.h"
#include "rapidjson/writer.h"

namespace broker_service::cobalt {

namespace {

// field name for cobalt project information.
constexpr std::string_view kObjectProject = "project";
constexpr std::string_view kFieldProjectName = "name";
constexpr std::string_view kFieldUpdateInterval = "update_interval_seconds";

// field name for cobalt metric information.
constexpr std::string_view kArrayMetrics = "metrics";
constexpr std::string_view kFieldMetricId = "id";
constexpr std::string_view kFieldMetricType = "metric_type";

// field name for cobalt mapping information.
constexpr std::string_view kArrayMappings = "mappings";
constexpr std::string_view kFieldMappingMetricId = "metric_id";
constexpr std::string_view kFieldMappingEventCodes = "event_codes";
constexpr std::string_view kFieldMappingPath = "path";

// field name for event_code value.
constexpr std::string_view kFieldEventCodeValue = "value";

}  // namespace

CobaltConfigReader::CobaltConfigReader(rapidjson::Document document,
                                       rapidjson::SchemaDocument* schema)
    : broker_service::JsonReader(std::move(document), schema) {}

std::optional<const ProjectConfig*> CobaltConfigReader::ReadProject() {
  if (!IsOk()) {
    return std::nullopt;
  }

  if (project_config_ != nullptr) {
    return project_config_.get();
  }

  if (!project_.has_value()) {
    project_ = document_.FindMember(kObjectProject.data());
  }

  auto name_it = project_.value()->value.FindMember(kFieldProjectName.data());
  auto update_interval_sec_it = project_.value()->value.FindMember(kFieldUpdateInterval.data());

  project_config_ = std::make_unique<ProjectConfig>(name_it->value.GetString(),
                                                    update_interval_sec_it->value.GetUint64());
  return project_config_.get();
}

std::optional<const MetricConfig*> CobaltConfigReader::ReadNextMetric() {
  if (!IsOk()) {
    return std::nullopt;
  }

  if (!metrics_.has_value()) {
    metrics_ = document_.FindMember(kArrayMetrics.data());
  }

  if (!current_metric_.has_value()) {
    current_metric_ = metrics_.value()->value.Begin();
  }

  if (current_metric_ == metrics_.value()->value.End()) {
    return std::nullopt;
  }

  auto id_it = current_metric_.value()->FindMember(kFieldMetricId.data());
  uint64_t id = id_it->value.GetUint64();
  if (project_config_->Find(id).has_value()) {
    std::string error =
        std::string("Duplicated metric id ") + std::to_string(id_it->value.GetUint64());
    error_messages_.emplace_back(error);
    return std::nullopt;
  }

  auto metric_type_it = current_metric_.value()->FindMember(kFieldMetricType.data());
  if (!metric_type_it->value.IsString()) {
    rapidjson::StringBuffer sb;
    sb.Clear();
    rapidjson::Writer<rapidjson::StringBuffer> writer(sb);
    metric_type_it->name.Accept(writer);
    std::string json(sb.GetString());
    error_messages_.emplace_back(json);
    return std::nullopt;
  }
  SupportedType metric_type = GetSupportedType(metric_type_it->value.GetString());

  if (metric_type == SupportedType::kUnknown) {
    std::string error =
        std::string("Unsupported metric type on metric with id: ") + id_it->value.GetString();
    error_messages_.emplace_back(error);
    return std::nullopt;
  }
  const auto* metric_config = project_config_->FindOrCreate(id, metric_type).value();
  ++current_metric_.value();
  return metric_config;
}

std::optional<JsonMapping> CobaltConfigReader::ReadNextMapping() {
  if (!IsOk()) {
    return std::nullopt;
  }

  if (!mappings_.has_value()) {
    mappings_ = document_.FindMember(kArrayMappings.data());
  }

  if (!current_mapping_.has_value()) {
    current_mapping_ = mappings_.value()->value.Begin();
  }

  if (current_mapping_.value() == mappings_.value()->value.End()) {
    return std::nullopt;
  }

  auto id_it = current_mapping_.value()->FindMember(kFieldMappingMetricId.data());
  uint64_t id = id_it->value.GetUint64();
  auto metric_config = project_config_->Find(id);
  if (!metric_config.has_value()) {
    std::string error =
        std::string("Mapping referencing unknown metric_id ") + id_it->value.GetString();
    return std::nullopt;
  }

  auto path_it = current_mapping_.value()->FindMember(kFieldMappingPath.data());
  std::string path = path_it->value.GetString();
  if (metric_config.value()->GetEventCodes(path).has_value()) {
    std::string error =
        std::string("Multiple event codes mapped to the same path for the same metric.\n path: ") +
        path;
    error_messages_.emplace_back(error);
    return std::nullopt;
  }

  auto event_codes_it = current_mapping_.value()->FindMember(kFieldMappingEventCodes.data());
  if (static_cast<uint64_t>(event_codes_it->value.End() - event_codes_it->value.Begin()) >
      kMaxDimensionsPerEvent) {
    std::string error = std::string("Mapping with metric_id: ") + std::to_string(id) +
                        std::string(" exceeds maximum amount of event codes.");
    error_messages_.emplace_back(error);
    return std::nullopt;
  }

  JsonMapping mapping = {};
  mapping.metric_id = id;
  mapping.path = path;
  uint64_t current = 0;
  for (auto event_code_it = event_codes_it->value.Begin();
       event_code_it != event_codes_it->value.End(); ++event_code_it, ++current) {
    if (event_code_it->IsNull()) {
      continue;
    }
    auto val_it = event_code_it->FindMember(kFieldEventCodeValue.data());
    if (val_it == event_code_it->MemberEnd() || val_it->value.IsNull()) {
      continue;
    }

    mapping.codes.codes[current] = val_it->value.GetUint64();
  }

  metric_config.value()->InsertOrUpdate(path, mapping.codes);
  ++current_mapping_.value();
  return mapping;
}

void CobaltConfigReader::Reset() {
  // Clear all state.
  validator_.Reset();
  project_config_.reset();
  project_.reset();
  metrics_.reset();
  mappings_.reset();
  current_metric_.reset();
  current_mapping_.reset();
}

std::optional<std::unique_ptr<ProjectConfig>> CobaltConfigReader::MakeProjectAndReset() {
  if (!ReadProject().has_value()) {
    return std::nullopt;
  }

  while (ReadNextMetric().has_value()) {
  }
  if (!IsOk()) {
    return std::nullopt;
  }

  while (ReadNextMapping().has_value()) {
  }
  if (!IsOk()) {
    return std::nullopt;
  }

  auto project = std::move(project_config_);
  Reset();
  return std::move(project);
}

}  // namespace broker_service::cobalt
