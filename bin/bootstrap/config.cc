// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/bootstrap/config.h"

#include <utility>

#include "lib/ftl/files/file.h"

namespace bootstrap {
namespace {

constexpr char kApps[] = "apps";
constexpr char kServices[] = "services";

}  // namespace

Config::Config() = default;

Config::~Config() = default;

bool Config::ReadFrom(const std::string& config_file) {
  std::string data;
  return files::ReadFileToString(config_file, &data) && Parse(data);
}

bool Config::Parse(const std::string& string) {
  modular::JsonDoc document;
  document.Parse(string);
  if (!document.IsObject())
    return false;

  auto services_it = document.FindMember(kServices);
  if (services_it != document.MemberEnd()) {
    const auto& value = services_it->value;
    if (!value.IsObject())
      return false;
    for (const auto& reg : value.GetObject()) {
      if (!reg.name.IsString())
        return false;
      std::string service_name = reg.name.GetString();
      auto launch_info = GetLaunchInfo(reg.value);
      if (!launch_info)
        return false;
      services_.emplace(service_name, std::move(launch_info));
    }
  }

  auto apps_it = document.FindMember(kApps);
  if (apps_it != document.MemberEnd()) {
    const auto& value = apps_it->value;
    if (!value.IsArray())
      return false;
    for (const auto& app : value.GetArray()) {
      auto launch_info = GetLaunchInfo(app);
      if (!launch_info)
        return false;
      apps_.push_back(std::move(launch_info));
    }
  }

  return true;
}

app::ApplicationLaunchInfoPtr Config::GetLaunchInfo(
    const modular::JsonValue& value) {
  auto launch_info = app::ApplicationLaunchInfo::New();
  if (value.IsString()) {
    launch_info->url = value.GetString();
  } else if (value.IsArray()) {
    const auto& array = value.GetArray();
    if (array.Empty() || !array[0].IsString())
      return nullptr;
    launch_info->url = array[0].GetString();
    for (size_t i = 1; i < array.Size(); ++i) {
      if (!array[i].IsString())
        return nullptr;
      launch_info->arguments.push_back(array[i].GetString());
    }
  } else {
    return nullptr;
  }
  return launch_info;
}

}  // namespace bootstrap
