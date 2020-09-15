// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/run_test_component/max_severity_config.h"

#include <lib/syslog/logger.h>

#include <rapidjson/document.h>
#include <src/lib/fxl/strings/string_printf.h>

#include "garnet/bin/run_test_component/run_test_component.h"

using fxl::StringPrintf;

namespace run {

const std::string kTests = "tests";
const std::string kUrl = "url";
const std::string kMaxSeverity = "max_severity";

MaxSeverityConfig MaxSeverityConfig::ParseFromDirectory(const std::string& path) {
  MaxSeverityConfig config;
  config.ParseDirectory(path);

  return config;
}

void MaxSeverityConfig::ParseDirectory(const std::string& path) {
  auto cb = [this](rapidjson::Document document) { ParseDocument(std::move(document)); };
  json_parser_.ParseFromDirectory(path, cb);
}

void MaxSeverityConfig::ParseDocument(rapidjson::Document document) {
  if (!document.IsObject()) {
    json_parser_.ReportError("Config file is not a JSON object.");
    return;
  }

  auto tests_it = document.FindMember(kTests);
  if (tests_it != document.MemberEnd()) {
    auto& value = tests_it->value;
    if (value.IsArray()) {
      for (const auto& test : value.GetArray()) {
        auto keys = {kUrl, kMaxSeverity};
        for (const auto& key : keys) {
          if (!test.HasMember(key)) {
            json_parser_.ReportError(StringPrintf("'%s' not found", key.c_str()));
            return;
          }
          if (!test[key].IsString()) {
            json_parser_.ReportError(StringPrintf("'%s' is not a string", key.c_str()));
            return;
          }
        }
        std::string url = test[kUrl].GetString();
        std::string severity = test[kMaxSeverity].GetString();
        int32_t log_severity;
        auto level_result = run::ParseLogLevel(severity);
        if (level_result.is_ok()) {
          log_severity = level_result.value();
        } else {
          json_parser_.ReportError(StringPrintf(
              "'%s' is not a valid severity for %s. Must be one of: [TRACE, DEBUG, INFO, "
              "WARN, ERROR, FATAL]",
              severity.c_str(), url.c_str()));
          return;
        }
        auto emplace_out = config_.emplace(url, log_severity);
        if (!emplace_out.second) {
          json_parser_.ReportError(StringPrintf("test %s configured twice.", url.c_str()));
          return;
        }
      }
    } else {
      json_parser_.ReportError(StringPrintf("'%s' is not an array.", kTests.c_str()));
    }
  }
}

}  // namespace run
