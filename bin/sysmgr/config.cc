// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/config.h"

#include <utility>

#include "lib/fxl/files/file.h"
#include "third_party/rapidjson/rapidjson/document.h"

namespace sysmgr {
namespace {

constexpr char kAppLoaders[] = "loaders";
constexpr char kApps[] = "apps";
constexpr char kServices[] = "services";
constexpr char kStartupServices[] = "startup_services";

fuchsia::sys::LaunchInfoPtr GetLaunchInfo(
    const rapidjson::Document::ValueType& value) {
  auto launch_info = fuchsia::sys::LaunchInfo::New();
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

bool ParseServiceMap(const rapidjson::Document& document,
                     const std::string& key, Config::ServiceMap* services) {
  auto it = document.FindMember(key);
  if (it != document.MemberEnd()) {
    const auto& value = it->value;
    if (!value.IsObject())
      return false;
    for (const auto& reg : value.GetObject()) {
      if (!reg.name.IsString())
        return false;
      std::string service_key = reg.name.GetString();
      auto launch_info = GetLaunchInfo(reg.value);
      if (!launch_info)
        return false;
      services->emplace(service_key, std::move(launch_info));
    }
  }
  return true;
}

}  // namespace

Config::Config() = default;

Config::Config(Config&& other) = default;

Config& Config::operator=(Config&& other) = default;

Config::~Config() = default;

bool Config::ReadFrom(const std::string& config_file) {
  std::string data;
  return files::ReadFileToString(config_file, &data) &&
         Parse(data, config_file);
}

bool Config::Parse(const std::string& string, const std::string& config_file) {
  rapidjson::Document document;
  document.Parse(string);
  FXL_CHECK(!document.HasParseError())
      << "Could not parse file at " << config_file;
  if (!(document.IsObject() &&
        ParseServiceMap(document, kServices, &services_) &&
        ParseServiceMap(document, kAppLoaders, &app_loaders_)))
    return false;

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

  auto startup_services_it = document.FindMember(kStartupServices);
  if (startup_services_it != document.MemberEnd()) {
    const auto& value = startup_services_it->value;
    if (!value.IsArray())
      return false;
    for (const auto& service : value.GetArray()) {
      if (!service.IsString())
        return false;
      startup_services_.push_back(service.GetString());
    }
  }

  return true;
}

}  // namespace sysmgr
