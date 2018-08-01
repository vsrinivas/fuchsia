// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include <string>
#include <vector>

#include "lib/fxl/files/file.h"
#include "lib/fxl/strings/concatenate.h"
#include "lib/fxl/strings/join_strings.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

const char SchemeMap::kConfigDirPath[] =
    "/system/data/appmgr/scheme_map/";

bool SchemeMap::ParseFromDirectory(const std::string& path) {
  internal_map_.clear();
  auto cb = [this] (rapidjson::Document document) {
    ParseDocument(std::move(document));
  };
  json_parser_.ParseFromDirectory(path, cb);
  return !json_parser_.HasError();
}

void SchemeMap::ParseDocument(rapidjson::Document document) {
  if (!document.IsObject()) {
    json_parser_.ReportError("Document is not a valid object.");
    return;
  }
  auto launchers = document.FindMember("launchers");
  if (launchers == document.MemberEnd()) {
    json_parser_.ReportError("Missing 'launchers'.");
    return;
  }
  if (!launchers->value.IsObject()) {
    json_parser_.ReportError("'launchers' is not a valid object.");
    return;
  }

  for (auto it = launchers->value.MemberBegin();
       it != launchers->value.MemberEnd(); ++it) {
    const std::string& launcher = it->name.GetString();
    if (!it->value.IsArray()) {
      json_parser_.ReportError(fxl::StringPrintf(
          "Schemes for '%s' are not a list.", launcher.c_str()));
      return;
    }
    for (const auto& scheme : it->value.GetArray()) {
      if (!scheme.IsString()) {
        json_parser_.ReportError(fxl::StringPrintf(
            "Scheme for '%s' is not a string.", launcher.c_str()));
      } else {
        if (internal_map_.count(scheme.GetString()) > 0) {
          json_parser_.ReportError(fxl::StringPrintf(
              "Scheme '%s' is assigned to two launchers.", scheme.GetString()));
        }
        internal_map_[scheme.GetString()] = launcher;
      }
    }
  }
}

std::string SchemeMap::LookUp(const std::string& scheme) const {
  if (internal_map_.count(scheme) == 0) {
    return "";
  }
  return internal_map_.find(scheme)->second;
}

}  // namespace component