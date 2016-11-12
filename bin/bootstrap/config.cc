// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/bootstrap/config.h"

#include <utility>

#include <rapidjson/document.h>

#include "lib/ftl/files/file.h"

namespace bootstrap {
namespace {

constexpr char kServices[] = "services";

}  // namespace

Config::Config() = default;

Config::~Config() = default;

bool Config::ReadFrom(const std::string& config_file) {
  std::string data;
  return files::ReadFileToString(config_file, &data) && Parse(data);
}

bool Config::Parse(const std::string& string) {
  rapidjson::Document document;
  document.Parse(string.data(), string.size());
  if (!document.IsObject())
    return false;

  auto service_it = document.FindMember(kServices);
  if (service_it != document.MemberEnd()) {
    const auto& value = service_it->value;
    if (!value.IsObject())
      return false;
    for (const auto& reg : value.GetObject()) {
      if (!reg.name.IsString())
        return false;

      std::string service_name = reg.name.GetString();

      auto launch_info = modular::ApplicationLaunchInfo::New();
      if (reg.value.IsString()) {
        launch_info->url = reg.value.GetString();
      } else if (reg.value.IsArray()) {
        const auto& array = reg.value.GetArray();
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

      services_.emplace(service_name, std::move(launch_info));
    }
  }
  return true;
}

}  // namespace bootstrap
