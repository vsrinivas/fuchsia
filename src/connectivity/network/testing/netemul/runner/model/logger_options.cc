// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "logger_options.h"

#include <src/lib/fxl/strings/string_printf.h>

namespace netemul {
namespace config {

static const char* kEnabled = "enabled";
static const char* kKlogsEnabled = "klogs_enabled";
static const char* kFilters = "filters";

static const bool kDefaultEnabled = true;
static const bool kDefaultKlogsEnabled = false;

/*
 * LoggerOptions
 *
 * public
 */

LoggerOptions::LoggerOptions() { SetDefaults(); }

bool LoggerOptions::ParseFromJSON(const rapidjson::Value& value,
                                  json::JSONParser* parser) {
  if (value.IsNull()) {
    SetDefaults();
    return true;
  }

  if (!value.IsObject()) {
    parser->ReportError("logger_options must be object type");
    return false;
  }

  SetDefaults();

  for (auto i = value.MemberBegin(); i != value.MemberEnd(); i++) {
    if (i->name == kEnabled) {
      if (!i->value.IsBool()) {
        parser->ReportError("logger_options enabled must be a boolean value");
        return false;
      }
      enabled_ = i->value.GetBool();
    } else if (i->name == kKlogsEnabled) {
      if (!i->value.IsBool()) {
        parser->ReportError(
            "logger_options klogs_enabled must be boolean value");
        return false;
      }
      klogs_enabled_ = i->value.GetBool();
    } else if (i->name == kFilters) {
      if (!filters_.ParseFromJSON(i->value, parser)) {
        return false;
      }
    } else {
      parser->ReportError(fxl::StringPrintf(
          "Unrecognized logger_options member \"%s\"", i->name.GetString()));
      return false;
    }
  }

  return true;
}

void LoggerOptions::SetDefaults() {
  enabled_ = kDefaultEnabled;
  klogs_enabled_ = kDefaultKlogsEnabled;
  filters_.SetDefaults();
}

bool LoggerOptions::enabled() const { return enabled_; }

bool LoggerOptions::klogs_enabled() const { return klogs_enabled_; }

const LoggerFilterOptions& LoggerOptions::filters() const { return filters_; }

}  // namespace config
}  // namespace netemul
