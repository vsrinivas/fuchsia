// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/application_manager/startup_config.h"

#include <utility>

#include <rapidjson/document.h>

namespace modular {
namespace {

constexpr char kInitialApps[] = "initial-apps";

}  // namespace

StartupConfig::StartupConfig() = default;

StartupConfig::~StartupConfig() = default;

bool StartupConfig::Parse(const std::string& string) {
  initial_apps_.clear();

  rapidjson::Document document;
  document.Parse(string.data(), string.size());
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

  return true;
}

std::vector<ApplicationLaunchInfoPtr> StartupConfig::TakeInitialApps() {
  return std::move(initial_apps_);
}

}  // namespace modular
