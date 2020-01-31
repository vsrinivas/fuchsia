// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_INSPECT_INSPECT_CONFIG_READER_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_INSPECT_INSPECT_CONFIG_READER_H_

#include <memory>
#include <optional>

#include "garnet/bin/metric_broker/config/inspect/snapshot_config.h"
#include "garnet/bin/metric_broker/config/json_reader.h"
#include "rapidjson/document.h"
#include "rapidjson/schema.h"

namespace broker_service::inspect {

// This class parses a valid JSON document that conforms to |inspect.schema.json|
// and returns a |SnapshotCofig|.
class InspectConfigReader final : public broker_service::JsonReader {
 public:
  InspectConfigReader(rapidjson::Document document, rapidjson::SchemaDocument* schema);
  InspectConfigReader(const InspectConfigReader&) = delete;
  InspectConfigReader(InspectConfigReader&&) = delete;
  InspectConfigReader& operator=(const InspectConfigReader&) = delete;
  InspectConfigReader& operator=(InspectConfigReader&&) = delete;
  ~InspectConfigReader() final = default;

  // Returns the parsed |SnapshotConfig|.
  // Returns |std::nullopt| if |InspectConfigReader::IsOk| is false.
  std::optional<std::unique_ptr<SnapshotConfig>> MakeSnapshotConfigAndReset();

  // Resets the parser.
  void Reset();

 private:
  std::unique_ptr<SnapshotConfig> snapshot_config_ = nullptr;
};

}  // namespace broker_service::inspect

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_INSPECT_INSPECT_CONFIG_READER_H_
