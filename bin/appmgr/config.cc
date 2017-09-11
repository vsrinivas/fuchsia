// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/config.h"

#include <stdio.h>
#include <utility>

#include "lib/fxl/files/file.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace app {
namespace {

constexpr char kInitialApps[] = "initial-apps";
constexpr char kPath[] = "path";
constexpr char kInclude[] = "include";

}  // namespace

bool Config::ReadIfExistsFrom(const std::string& config_file) {
  std::string data;
  if (!files::ReadFileToString(config_file, &data)) {
    fprintf(stderr, "appmgr: Ignoring missing config file: %s\n",
            config_file.c_str());
    return true;
  }
  if (!Parse(data)) {
    fprintf(stderr, "appmgr: Failed to parse config file: %s\n",
            config_file.c_str());
    return false;
  }
  return true;
}

bool Config::Parse(const std::string& string) {
  initial_apps_.clear();

  rapidjson::Document document;
  document.Parse(string);
  if (!document.IsObject())
    return false;

  auto inital_apps_it = document.FindMember(kInitialApps);
  if (inital_apps_it != document.MemberEnd()) {
    const auto& value = inital_apps_it->value;
    if (!value.IsArray())
      return false;
    for (const auto& application : value.GetArray()) {
      auto launch_info = ApplicationLaunchInfo::New();
      if (application.IsString()) {
        launch_info->url = application.GetString();
      } else if (application.IsArray()) {
        const auto& array = application.GetArray();
        if (array.Empty() || !array[0].IsString())
          return false;
        launch_info->url = array[0].GetString();
        for (size_t i = 1; i < array.Size(); ++i) {
          if (!array[i].IsString())
            return false;
          launch_info->arguments.push_back(array[i].GetString());
        }
      } else {
        return false;
      }
      initial_apps_.push_back(std::move(launch_info));
    }
  }

  auto path_it = document.FindMember(kPath);
  if (path_it != document.MemberEnd()) {
    const auto& value = path_it->value;
    if (!value.IsArray())
      return false;
    for (const auto& dir : value.GetArray()) {
      if (!dir.IsString())
        return false;
      path_.push_back(dir.GetString());
    }
  }

  auto include_it = document.FindMember(kInclude);
  if (include_it != document.MemberEnd()) {
    const auto& value = include_it->value;
    if (!value.IsArray())
      return false;
    for (const auto& file : value.GetArray()) {
      if (!file.IsString())
        return false;
      if (!ReadIfExistsFrom(file.GetString()))
        return false;
    }
  }

  return true;
}

std::vector<std::string> Config::TakePath() {
  return std::move(path_);
}

std::vector<ApplicationLaunchInfoPtr> Config::TakeInitialApps() {
  return std::move(initial_apps_);
}

}  // namespace app
