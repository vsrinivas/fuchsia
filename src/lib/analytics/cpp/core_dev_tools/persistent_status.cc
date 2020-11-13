// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/persistent_status.h"

#include "sdk/lib/syslog/cpp/macros.h"
#include "src/lib/analytics/cpp/metric_properties/metric_properties.h"
#include "src/lib/uuid/uuid.h"

namespace analytics::core_dev_tools::internal {

namespace {

constexpr char kEnabledProperty[] = "analytics-status";
constexpr char kUuidProperty[] = "uuid";

}  // namespace

PersistentStatus::PersistentStatus(std::string_view tool_name)
    : tool_name_(tool_name), launched_property_(tool_name_ + "-launched") {}

void PersistentStatus::Enable() {
  metric_properties::SetBool(kEnabledProperty, true);
  metric_properties::Set(kUuidProperty, uuid::Uuid::Generate().ToString());
}

void PersistentStatus::Disable() {
  metric_properties::SetBool(kEnabledProperty, false);
  metric_properties::Delete(kUuidProperty);
}

bool PersistentStatus::IsEnabled() {
  auto is_enabled = metric_properties::GetBool(kEnabledProperty);
  FX_DCHECK(is_enabled.has_value());
  return is_enabled.value_or(false);
}

bool PersistentStatus::IsFirstLaunchOfFirstTool() {
  return !metric_properties::Exists(kEnabledProperty);
}

void PersistentStatus::MarkAsDirectlyLaunched() { metric_properties::Set(launched_property_, ""); }

bool PersistentStatus::IsFirstDirectLaunch() const {
  return !metric_properties::Exists(launched_property_);
}

std::string PersistentStatus::GetUuid() {
  auto uuid = metric_properties::Get(kUuidProperty);
  FX_DCHECK(uuid.has_value());
  return uuid.value_or(std::string());
}

}  // namespace analytics::core_dev_tools::internal
