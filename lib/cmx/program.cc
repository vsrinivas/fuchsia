// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/program.h"

#include <algorithm>

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

constexpr char kBinary[] = "binary";
constexpr char kData[] = "data";

bool ProgramMetadata::Parse(const rapidjson::Value& program_value,
                            json::JSONParser* json_parser) {
  binary_.clear();
  data_.clear();
  binary_null_ = true;
  data_null_ = true;

  if (!program_value.IsObject()) {
    json_parser->ReportError("Program is not an object.");
    return false;
  }

  const bool parsed_binary = ParseBinary(program_value, json_parser);
  const bool parsed_data = ParseData(program_value, json_parser);
  if (!parsed_binary && !parsed_data) {
    json_parser->ReportError(
        "Both 'binary' and 'data' in program are missing.");
    return false;
  }

  return true;
}

bool ProgramMetadata::ParseBinary(const rapidjson::Value& program_value,
                                  json::JSONParser* json_parser) {
  const auto binary = program_value.FindMember(kBinary);
  if (binary == program_value.MemberEnd()) {
    return false;
  }
  if (!binary->value.IsString()) {
    json_parser->ReportError("'binary' in program is not a string.");
    return false;
  }
  binary_ = binary->value.GetString();
  binary_null_ = false;
  return true;
}

bool ProgramMetadata::ParseData(const rapidjson::Value& program_value,
                                json::JSONParser* json_parser) {
  const auto data = program_value.FindMember(kData);
  if (data == program_value.MemberEnd()) {
    return false;
  }
  if (!data->value.IsString()) {
    json_parser->ReportError("'data' in program is not a string.");
    return false;
  }
  data_ = data->value.GetString();
  data_null_ = false;
  return true;
}

}  // namespace component
