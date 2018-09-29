// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fxl/logging.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/strings/string_printf.h>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/schema.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "session_result_spec.h"

namespace cpuperf {

namespace {

// Top-level schema.
const char kRootSchema[] = R"({
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "config_name": {
      "type": "string"
    },
    "num_iterations": {
      "type": "integer",
      "minimum": 1
    },
    "num_traces": {
      "type": "integer",
      "minimum": 1
    },
    "output_path_prefix": {
      "type": "string"
    }
  }
})";

const char kConfigNameKey[] = "config_name";
const char kNumIterationsKey[] = "num_iterations";
const char kNumTracesKey[] = "num_traces";
const char kOutputPathPrefixKey[] = "output_path_prefix";

std::unique_ptr<rapidjson::SchemaDocument> InitSchema(const char schemaSpec[]) {
  rapidjson::Document schema_document;
  if (schema_document.Parse(schemaSpec).HasParseError()) {
    FXL_DCHECK(false) << "Schema validation spec itself is not valid JSON.";
    return nullptr;
  }
  return std::make_unique<rapidjson::SchemaDocument>(schema_document);
}

bool ValidateSchema(const rapidjson::Value& value,
                    const rapidjson::SchemaDocument& schema) {
  rapidjson::SchemaValidator validator(schema);
  if (!value.Accept(validator)) {
    rapidjson::StringBuffer uri_buffer;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(uri_buffer);
    FXL_LOG(ERROR) << "Incorrect schema of session config at "
                   << uri_buffer.GetString() << " , schema violation: "
                   << validator.GetInvalidSchemaKeyword();
    return false;
  }
  return true;
}

}

SessionResultSpec::SessionResultSpec(const std::string& config_name,
                                     size_t num_iterations, size_t num_traces,
                                     const std::string& output_path_prefix)
    : config_name(config_name),
      num_iterations(num_iterations),
      num_traces(num_traces),
      output_path_prefix(output_path_prefix) {
}

// Given an iteration number and trace number, return the output file.
std::string SessionResultSpec::GetTraceFilePath(size_t iter_num,
                                                size_t trace_num) const {
  return fxl::StringPrintf("%s.%zu.%zu.cpuperf",
                           output_path_prefix.c_str(), iter_num, trace_num);
}

bool DecodeSessionResultSpec(const std::string& json,
                             SessionResultSpec* out_spec) {
  // Initialize schemas for JSON validation.
  auto root_schema = InitSchema(kRootSchema);
  if (!root_schema) {
    return false;
  }

  SessionResultSpec result;
  rapidjson::Document document;
  document.Parse<rapidjson::kParseCommentsFlag>(json.c_str(), json.size());
  if (document.HasParseError()) {
    auto offset = document.GetErrorOffset();
    auto code = document.GetParseError();
    FXL_LOG(ERROR) << "Couldn't parse the session result spec file: offset "
                   << offset << ", " << GetParseError_En(code);
    return false;
  }
  if (!ValidateSchema(document, *root_schema)) {
    return false;
  }

  if (document.HasMember(kConfigNameKey)) {
    result.config_name = document[kConfigNameKey].GetString();
  }

  if (document.HasMember(kNumIterationsKey)) {
    result.num_iterations = document[kNumIterationsKey].GetUint();
  }

  if (document.HasMember(kNumTracesKey)) {
    result.num_traces = document[kNumTracesKey].GetUint();
  }

  if (document.HasMember(kOutputPathPrefixKey)) {
    result.output_path_prefix = document[kOutputPathPrefixKey].GetString();
  }

  *out_spec = std::move(result);
  return true;
}

bool WriteSessionResultSpec(const std::string& output_file_path,
                            const SessionResultSpec& spec) {
  rapidjson::StringBuffer string_buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(string_buffer);

  writer.StartObject();

  if (!spec.config_name.empty()) {
    writer.Key(kConfigNameKey);
    writer.String(spec.config_name.c_str());
  }

  writer.Key(kNumIterationsKey);
  writer.Uint(spec.num_iterations);

  writer.Key(kNumTracesKey);
  writer.Uint(spec.num_traces);

  writer.Key(kOutputPathPrefixKey);
  writer.String(spec.output_path_prefix.c_str());

  writer.EndObject();

  std::string encoded = string_buffer.GetString();
  if (!files::WriteFile(output_file_path, encoded.data(), encoded.size())) {
    FXL_LOG(ERROR) << "Error writing to: " << output_file_path;
    return false;
  }
  return true;
}

}  // namespace cpuperf
