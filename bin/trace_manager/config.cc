// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include "apps/tracing/src/trace_manager/config.h"
#include "lib/ftl/logging.h"

namespace tracing {

bool Config::ReadFrom(const std::string& config_file) {
  std::ifstream in(config_file);
  rapidjson::IStreamWrapper isw(in);
  rapidjson::Document document;

  if (!document.ParseStream(isw).IsObject()) {
    FTL_LOG(ERROR) << "Failed to parse JSON object from: " << config_file;
    return false;
  }

  Config config;
  for (auto it = document.MemberBegin(); it != document.MemberEnd(); ++it) {
    if (!(it->name.IsString() && it->value.IsString())) {
      FTL_LOG(ERROR) << "Expecting both name and value to be strings";
      return false;
    }
    config.known_categories[it->name.GetString()] = it->value.GetString();
  }

  *this = config;
  return true;
}

}  // namespace tracing
