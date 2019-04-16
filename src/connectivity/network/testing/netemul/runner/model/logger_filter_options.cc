// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger_filter_options.h"

#include <fuchsia/logger/cpp/fidl.h>
#include <src/lib/fxl/strings/string_printf.h>

namespace netemul {
namespace config {

static const char* kVerbosity = "verbosity";
static const char* kTags = "tags";

static const uint8_t kDefaultVerbosity = 0;

/*
 * LoggerFilterOptions
 *
 * public
 */

LoggerFilterOptions::LoggerFilterOptions() { SetDefaults(); }

bool LoggerFilterOptions::ParseFromJSON(const rapidjson::Value& value,
                                        json::JSONParser* parser) {
  if (value.IsNull()) {
    SetDefaults();
    return true;
  }

  if (!value.IsObject()) {
    parser->ReportError("logger_filter_options must be object type");
    return false;
  }

  SetDefaults();

  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kVerbosity) {
      if (!i->value.IsUint()) {
        parser->ReportError(
            "logger_options enabled must be a unsigned integer value");
        return false;
      }
      verbosity_ = i->value.GetUint();
    } else if (i->name == kTags) {
      if (i->value.IsNull()) {
        continue;
      }

      if (!i->value.IsArray()) {
        parser->ReportError("logger_options tags must be an array of strings");
        return false;
      }

      auto tags = i->value.GetArray();

      if (tags.Size() > fuchsia::logger::MAX_TAGS) {
        parser->ReportError(fxl::StringPrintf(
            "logger_options tags cannot have more than %d elements",
            fuchsia::logger::MAX_TAGS));
        return false;
      }

      for (auto& t : tags) {
        if (!t.IsString()) {
          parser->ReportError(
              "logger_options tags must be an array of strings");
          return false;
        }

        if (t.GetStringLength() > fuchsia::logger::MAX_TAG_LEN_BYTES) {
          parser->ReportError(fxl::StringPrintf(
              "logger_options tags cannot have more than %d bytes",
              fuchsia::logger::MAX_TAG_LEN_BYTES));
          return false;
        }

        tags_.emplace_back(t.GetString());
      }
    } else {
      parser->ReportError(
          fxl::StringPrintf("Unrecognized logger_filter_options member \"%s\"",
                            i->name.GetString()));
      return false;
    }
  }

  return true;
}

void LoggerFilterOptions::SetDefaults() {
  verbosity_ = kDefaultVerbosity;
  tags_.clear();
}

uint8_t LoggerFilterOptions::verbosity() const { return verbosity_; }

const std::vector<std::string>& LoggerFilterOptions::tags() const {
  return tags_;
}

}  // namespace config
}  // namespace netemul
