// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/cmx/runtime.h"

#include <lib/trace/event.h>

#include "rapidjson/document.h"
#include "src/lib/json_parser/json_parser.h"

namespace component {

constexpr char kRunner[] = "runner";

bool RuntimeMetadata::ParseFromFileAt(int dirfd, const std::string& file,
                                      json::JSONParser* json_parser) {
  TRACE_DURATION("cmx", "RuntimeMetadata::ParseFromFileAt", "file", file);
  runner_.clear();
  null_ = true;

  rapidjson::Document document = json_parser->ParseFromFileAt(dirfd, file);
  if (json_parser->HasError()) {
    return false;
  }
  return ParseFromDocument(document, json_parser);
}

bool RuntimeMetadata::ParseFromDocument(const rapidjson::Document& document,
                                        json::JSONParser* json_parser) {
  runner_.clear();
  null_ = true;

  const auto runner = document.FindMember(kRunner);
  if (runner == document.MemberEnd()) {
    // Valid config, but no runtime.
    return true;
  }
  if (!runner->value.IsString()) {
    json_parser->ReportError("'runner' is not a string.");
    return false;
  }
  runner_ = runner->value.GetString();
  null_ = false;
  return true;
}

}  // namespace component
