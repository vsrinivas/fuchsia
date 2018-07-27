// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/string_printf.h"
#include "lib/fxl/logging.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

bool SchemeMap::ParseFromFile(const std::string& file) {
  internal_map_.clear();

  const rapidjson::Document document = json_parser_.ParseFromFile(file);
  if (!document.IsObject()) {
    json_parser_.ReportError("Document is not a valid object.");
    return false;
  }
  auto launchers = document.FindMember("launchers");
  if (launchers == document.MemberEnd()) {
    json_parser_.ReportError("Missing 'launchers'.");
    return false;
  }
  if (!launchers->value.IsObject()) {
    json_parser_.ReportError("'launchers' is not a valid object.");
    return false;
  }

  for (auto it = launchers->value.MemberBegin();
       it != launchers->value.MemberEnd(); ++it) {
    const std::string& launcher = it->name.GetString();
    if (!it->value.IsArray()) {
      json_parser_.ReportError(fxl::StringPrintf(
          "Schemes for '%s' are not a list.", launcher.c_str()));
      return false;
    }
    for (const auto& scheme : it->value.GetArray()) {
      if (!scheme.IsString()) {
        json_parser_.ReportError(fxl::StringPrintf(
            "Scheme for '%s' is not a string.", launcher.c_str()));
      } else {
        internal_map_[scheme.GetString()] = launcher;
      }
    }
  }
  return !json_parser_.HasError();
}

std::string SchemeMap::LookUp(const std::string& scheme) const {
  if (internal_map_.count(scheme) == 0) {
    return "";
  }
  return internal_map_.find(scheme)->second;
}

// static
std::string SchemeMap::GetSchemeMapPath() {
  return "/system/data/appmgr/scheme_map.config";
}

}  // namespace component