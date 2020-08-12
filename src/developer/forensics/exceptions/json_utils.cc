// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/developer/forensics/exceptions/json_utils.h"

#include <lib/syslog/cpp/macros.h>

// TODO(fxbug.dev/57392): Move it back to //third_party once unification completes.
#include <zircon/third_party/rapidjson/include/rapidjson/document.h>
#include <zircon/third_party/rapidjson/include/rapidjson/error/en.h>

namespace forensics {
namespace exceptions {

std::set<std::string> ExtractFilters(const std::string& content) {
  rapidjson::Document document;
  if (rapidjson::ParseResult result = document.Parse(content.c_str()); result.IsError()) {
    FX_LOGS(ERROR) << "Parsing config as JSON at offset " << result.Offset() << ": "
                   << rapidjson::GetParseError_En(result.Code());
    return {};
  }

  if (!document.IsObject()) {
    FX_LOGS(ERROR) << "Config json is not an object.";
    return {};
  }

  if (!document.HasMember("filters"))
    return {};

  auto& filters = document["filters"];
  if (!filters.IsArray()) {
    FX_LOGS(WARNING) << "Filters member is not an array.";
    return {};
  }

  std::set<std::string> result_filters;
  for (uint32_t i = 0; i < filters.Size(); i++) {
    auto& filter = filters[i];
    if (!filter.IsString()) {
      FX_LOGS(WARNING) << "Filter " << i << " is not a string.";
      continue;
    }

    result_filters.insert(filter.GetString());
  }

  return result_filters;
}

}  // namespace exceptions
}  // namespace forensics
