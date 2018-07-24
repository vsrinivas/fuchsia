// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/config.h"

#include <string>
#include <utility>

#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/error/en.h"

namespace sysmgr {
namespace {

using fxl::StringPrintf;

constexpr char kAppLoaders[] = "loaders";
constexpr char kApps[] = "apps";
constexpr char kServices[] = "services";
constexpr char kStartupServices[] = "startup_services";

}  // namespace

bool Config::ParseFromFile(const std::string& config_file) {
  const rapidjson::Document document = json_parser_.ParseFromFile(config_file);
  if (!json_parser_.HasError()) {
    Parse(document);
  }
  return !json_parser_.HasError();
}

bool Config::ParseFromString(const std::string& data,
                             const std::string& pseudo_file) {
  const rapidjson::Document document =
      json_parser_.ParseFromString(data, pseudo_file);
  if (!json_parser_.HasError()) {
    Parse(document);
  }
  return !json_parser_.HasError();
}

bool Config::HasError() const {
  return json_parser_.HasError();
}

std::string Config::error_str() const {
  return json_parser_.error_str();
}

void Config::Parse(const rapidjson::Document& document) {
  if (!document.IsObject()) {
    json_parser_.ReportError("Config file is not a JSON object.");
    return;
  }

  if (!ParseServiceMap(document, kServices, &services_) ||
      !ParseServiceMap(document, kAppLoaders, &app_loaders_)) {
    return;
  }

  auto apps_it = document.FindMember(kApps);
  if (apps_it != document.MemberEnd()) {
    const auto& value = apps_it->value;
    const auto* name = apps_it->name.GetString();
    if (value.IsArray()) {
      for (const auto& app : value.GetArray()) {
        auto launch_info = GetLaunchInfo(app, name);
        if (launch_info) {
          apps_.push_back(std::move(launch_info));
        }
      }
    } else {
      json_parser_.ReportError(StringPrintf("'%s' is not an array.", name));
    }
  }

  auto startup_services_it = document.FindMember(kStartupServices);
  if (startup_services_it != document.MemberEnd()) {
    const auto& value = startup_services_it->value;
    const auto* name = startup_services_it->name.GetString();
    if (value.IsArray() &&
        std::all_of(
            value.GetArray().begin(), value.GetArray().end(),
            [](const rapidjson::Value& val) { return val.IsString(); })) {
      for (const auto& service : value.GetArray()) {
        startup_services_.push_back(service.GetString());
      }
    } else {
      json_parser_.ReportError(
          StringPrintf("'%s' is not an array of strings.", name));
    }
  }
}

bool Config::ParseServiceMap(const rapidjson::Document& document,
                             const std::string& key,
                             Config::ServiceMap* services) {
  auto it = document.FindMember(key);
  if (it != document.MemberEnd()) {
    const auto& value = it->value;
    if (!value.IsObject()) {
      json_parser_.ReportError(StringPrintf("'%s' must be an object.",
                                            key.c_str()));
      return false;
    }
    for (const auto& reg : value.GetObject()) {
      if (!reg.name.IsString()) {
        json_parser_.ReportError(
            StringPrintf("Keys of '%s' must be strings.", key.c_str()));
        continue;
      }
      std::string service_key = reg.name.GetString();
      auto launch_info = GetLaunchInfo(
          reg.value, StringPrintf("%s.%s", key.c_str(), service_key.c_str()));
      if (launch_info) {
        services->emplace(service_key, std::move(launch_info));
      }
    }
  }
  return !json_parser_.HasError();
}

fuchsia::sys::LaunchInfoPtr Config::GetLaunchInfo(
    const rapidjson::Document::ValueType& value, const std::string& name) {
  auto launch_info = fuchsia::sys::LaunchInfo::New();
  if (value.IsString()) {
    launch_info->url = value.GetString();
    return launch_info;
  }

  if (value.IsArray()) {
    const auto& array = value.GetArray();
    // If the element is an array, ensure it is non-empty and all values are
    // strings.
    if (!array.Empty() && std::all_of(array.begin(), array.end(),
                                      [](const rapidjson::Value& val) {
                                        return val.IsString();
                                      })) {
      launch_info->url = array[0].GetString();
      for (size_t i = 1; i < array.Size(); ++i) {
        launch_info->arguments.push_back(array[i].GetString());
      }
      return launch_info;
    }
  }

  json_parser_.ReportError(StringPrintf(
      "'%s' must be a string or a non-empty array of strings.",
      name.c_str()));
  return nullptr;
}

}  // namespace sysmgr
