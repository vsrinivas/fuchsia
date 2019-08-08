// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "inspect_config_reader.h"

#include <memory>
#include <string_view>

#include "garnet/bin/metric_broker/config/inspect/snapshot_config.h"

namespace broker_service::inspect {
namespace {

constexpr std::string_view kFieldConsistencyCheck = "consistency_check";

}

InspectConfigReader::InspectConfigReader(rapidjson::Document document,
                                         rapidjson::SchemaDocument* schema)
    : broker_service::JsonReader(std::move(document), schema) {}

std::optional<std::unique_ptr<SnapshotConfig>> InspectConfigReader::MakeSnapshotConfigAndReset() {
  if (!IsOk()) {
    return std::nullopt;
  }

  if (snapshot_config_ != nullptr) {
    return std::move(snapshot_config_);
  }

  auto consistency_check_it = document_.FindMember(kFieldConsistencyCheck.data());
  bool consistency_check = consistency_check_it->value.GetBool();
  snapshot_config_ = std::make_unique<SnapshotConfig>(consistency_check);
  auto snapshot = std::move(snapshot_config_);
  Reset();

  return snapshot;
}

void InspectConfigReader::Reset() {
  validator_.Reset();
  snapshot_config_.reset();
}

}  // namespace broker_service::inspect
