// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/trace_manager/config.h"

#include <lib/syslog/cpp/macros.h>

#include <fstream>

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <rapidjson/istreamwrapper.h>

namespace tracing {
namespace {

constexpr char kCategories[] = "categories";

}  // namespace

Config::Config() = default;

Config::~Config() = default;

bool Config::ReadFrom(const std::string& config_file) {
  std::ifstream in(config_file);
  rapidjson::IStreamWrapper isw(in);
  rapidjson::Document document;

  if (!document.ParseStream<rapidjson::kParseCommentsFlag>(isw).IsObject()) {
    FX_LOGS(ERROR) << "Failed to parse JSON object from: " << config_file;
    if (document.HasParseError()) {
      FX_LOGS(ERROR) << "Parse error " << GetParseError_En(document.GetParseError()) << " ("
                     << document.GetErrorOffset() << ")";
    }
    return false;
  }

  auto categories_it = document.FindMember(kCategories);
  if (categories_it != document.MemberEnd()) {
    const auto& value = categories_it->value;
    if (!value.IsObject()) {
      FX_LOGS(ERROR) << "Expecting " << kCategories << " to be an object";
      return false;
    }
    for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
      if (!(it->name.IsString() && it->value.IsString())) {
        FX_LOGS(ERROR) << "Expecting both name and value to be strings";
        return false;
      }
      known_categories_[it->name.GetString()] = it->value.GetString();
    }
  }

  return true;
}

}  // namespace tracing
