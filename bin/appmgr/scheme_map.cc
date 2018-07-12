// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/scheme_map.h"

#include <lib/fxl/strings/concatenate.h>
#include <lib/fxl/logging.h>
#include "third_party/rapidjson/rapidjson/document.h"

namespace component {

bool SchemeMap::Parse(const std::string& data, std::string* error) {
  rapidjson::Document document;
  document.Parse(data);
  if (!document.IsObject()) {
    *error = "document is not a valid object";
    return false;
  }
  auto launchers = document.FindMember("launchers");
  if (launchers == document.MemberEnd()) {
    *error = "missing \"launchers\"";
    return false;
  }
  if (!launchers->value.IsObject()) {
    *error = "\"launchers\" is not a valid object";
    return false;
  }

  for (auto it = launchers->value.MemberBegin();
       it != launchers->value.MemberEnd(); ++it) {
    const std::string& launcher = it->name.GetString();
    if (!it->value.IsArray()) {
      *error = fxl::Concatenate(
          {"schemes for \"", launcher, "\" are not a list"});
      return false;
    }
    for (const auto& scheme : it->value.GetArray()) {
      if (!scheme.IsString()) {
        *error = fxl::Concatenate(
            {"scheme for \"", launcher, "\" is not a string"});
        return false;
      }
      internal_map_[scheme.GetString()] = launcher;
    }
  }
  parsed_ = true;
  return true;
}

std::string SchemeMap::LookUp(const std::string& scheme) const {
  FXL_CHECK(parsed_);
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