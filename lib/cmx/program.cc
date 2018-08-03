// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/cmx/program.h"

#include <algorithm>

#include "garnet/lib/json/json_parser.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

constexpr char kBinary[] = "binary";

bool ProgramMetadata::Parse(const rapidjson::Value& program_value,
                            json::JSONParser* json_parser) {
  binary_.clear();
  null_ = true;

  if (!program_value.IsObject()) {
    json_parser->ReportError("Program is not an object.");
    return false;
  }

  const auto binary = program_value.FindMember(kBinary);
  if (binary == program_value.MemberEnd()) {
    json_parser->ReportError("'binary' in program is missing.");
    return false;
  }
  if (!binary->value.IsString()) {
    json_parser->ReportError("'binary' in program is not a string.");
    return false;
  }
  binary_ = binary->value.GetString();

  null_ = false;
  return true;
}

}  // namespace component
