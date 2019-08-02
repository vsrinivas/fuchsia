// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_CONFIG_READER_H_
#define GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_CONFIG_READER_H_

#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "garnet/bin/metric_broker/config/cobalt/event_codes.h"
#include "garnet/bin/metric_broker/config/cobalt/metric_config.h"
#include "garnet/bin/metric_broker/config/cobalt/project_config.h"
#include "rapidjson/document.h"
#include "rapidjson/schema.h"

namespace broker_service::cobalt {

// Helper struct for passing individual mappings.
struct JsonMapping {
  uint64_t metric_id = 0;
  std::string path = {};
  EventCodes codes = {};
};

// This class provides a JSON reader for parsing a json Cobalt project config file.
//
// This class is thread_compatible.
// This class is not copyable or moveable.
// This class is not assignable.
class JsonReader {
 public:
  JsonReader(rapidjson::Document document, rapidjson::SchemaDocument* schema);
  JsonReader(const JsonReader&) = delete;
  JsonReader(JsonReader&&) = delete;
  JsonReader& operator=(const JsonReader&) = delete;
  JsonReader& operator=(JsonReader&&) = delete;
  ~JsonReader() = default;

  // Returns a fully parsed |ProjectConfig| from |document_| and resets all state on the parser.
  // Returns |std::nullopt| on error.
  std::optional<std::unique_ptr<ProjectConfig>> MakeProjectAndReset();

  // Returns a pointer to |ProjectConfig| as parsed from the document. This only contains the
  // metrics and mappings added so far.
  std::optional<const ProjectConfig*> ReadProject();

  // Returns a pointer to the next MetricConfig.
  // Returns |std::nullopt| if |JsonReader::IsOk()| is false or if
  // there are no more |MetricConfig|s.
  std::optional<const MetricConfig*> ReadNextMetric();

  // Returns a pointer to the next JsonMapping.
  std::optional<JsonMapping> ReadNextMapping();

  // Returns true if |document_| complies with |schema_|.
  // Needs to be called before any Read* Method.
  bool Validate();

  // Returns true if there has been no error parsing so far.
  [[nodiscard]] bool IsOk() const { return error_messages_.empty(); }

  // Returns the list of errors found while parsing the json.
  const std::vector<std::string>& error_messages() { return error_messages_; }

  // Resets the current project and metric config.
  void Reset();

 private:
  rapidjson::Document document_;
  rapidjson::SchemaValidator validator_;

  std::unique_ptr<ProjectConfig> project_config_ = nullptr;
  std::vector<std::string> error_messages_ = {};

  // State of the parser.
  std::optional<rapidjson::Document::MemberIterator> project_;
  std::optional<rapidjson::Document::MemberIterator> metrics_;
  std::optional<rapidjson::Document::MemberIterator> mappings_;
  std::optional<rapidjson::Document::ValueIterator> current_metric_;
  std::optional<rapidjson::Document::ValueIterator> current_mapping_;
};

}  // namespace broker_service::cobalt

#endif  // GARNET_BIN_METRIC_BROKER_CONFIG_COBALT_CONFIG_READER_H_
