// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/view_manager/config.h"

#include <utility>

#include "lib/ftl/files/file.h"

namespace view_manager {
namespace {

constexpr char kAssociates[] = "associates";

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

  auto associates_it = document.FindMember(kAssociates);
  if (associates_it != document.MemberEnd()) {
    const auto& value = associates_it->value;
    if (!value.IsArray())
      return false;
    for (const auto& associate : value.GetArray()) {
      auto launch_info = GetLaunchInfo(associate);
      if (!launch_info)
        return false;
      associates_.push_back(std::move(launch_info));
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

}  // namespace view_manager
