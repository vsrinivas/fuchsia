// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/sysmgr/config.h"

#include <utility>

#include "lib/fxl/files/file.h"
#include "lib/fxl/functional/auto_call.h"
#include "lib/fxl/strings/string_printf.h"
#include "third_party/rapidjson/rapidjson/document.h"
#include "third_party/rapidjson/rapidjson/error/en.h"

namespace sysmgr {
namespace {

using ErrorCallback = std::function<void(size_t, const std::string&)>;
using fxl::StringPrintf;

constexpr char kAppLoaders[] = "loaders";
constexpr char kApps[] = "apps";
constexpr char kServices[] = "services";
constexpr char kStartupServices[] = "startup_services";

fuchsia::sys::LaunchInfoPtr GetLaunchInfo(
    const rapidjson::Document::ValueType& value, const std::string& name,
    const ErrorCallback& error_callback) {
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

  error_callback(
      0, StringPrintf("%s must be a string or a non-empty array of strings",
                      name.c_str()));
  return nullptr;
}

bool ParseServiceMap(const rapidjson::Document& document,
                     const std::string& key, Config::ServiceMap* services,
                     const ErrorCallback& error_callback) {
  bool has_error = false;
  auto it = document.FindMember(key);
  if (it != document.MemberEnd()) {
    const auto& value = it->value;
    if (!value.IsObject()) {
      error_callback(0, StringPrintf("%s must be an object", key.c_str()));
      return false;
    }
    for (const auto& reg : value.GetObject()) {
      if (!reg.name.IsString()) {
        error_callback(0, StringPrintf("%s keys must be strings", key.c_str()));
        has_error = true;
        continue;
      }
      std::string service_key = reg.name.GetString();
      auto launch_info = GetLaunchInfo(
          reg.value, StringPrintf("%s.%s", key.c_str(), service_key.c_str()),
          error_callback);
      if (launch_info) {
        services->emplace(service_key, std::move(launch_info));
      } else {
        has_error = true;
      }
    }
  }
  return !has_error;
}

void GetLineAndColumnForOffset(const std::string& input, size_t offset,
                               int32_t* output_line, int32_t* output_column) {
  if (offset == 0) {
    // Errors at position 0 are assumed to be related to the whole file.
    *output_line = 0;
    *output_column = 0;
    return;
  }
  *output_line = 1;
  *output_column = 1;
  for (size_t i = 0; i < input.size() && i < offset; i++) {
    if (input[i] == '\n') {
      *output_line += 1;
      *output_column = 1;
    } else {
      *output_column += 1;
    }
  }
}

}  // namespace

Config::Config() = default;

Config::Config(Config&& other) = default;

Config& Config::operator=(Config&& other) = default;

Config::~Config() = default;

bool Config::ReadFrom(const std::string& config_file) {
  std::string data;
  if (!files::ReadFileToString(config_file, &data)) {
    errors_.emplace_back(
        StringPrintf("Failed to read file %s", config_file.c_str()));
    return false;
  }
  Parse(data, config_file);
  return !HasErrors();
}

void Config::Parse(const std::string& string, const std::string& config_file) {
  errors_.clear();
  rapidjson::Document document;
  document.Parse(string);

  // If there is an error in parsing, store the incoming config for debug
  // purposes.
  auto store_debug_on_error = fxl::MakeAutoCall([&]() {
    if (HasErrors()) {
      config_data_ = string;
    }
  });

  auto error_callback = [this, &string](size_t offset,
                                        const std::string& error) {
    int32_t line;
    int32_t column;
    GetLineAndColumnForOffset(string, offset, &line, &column);
    if (line == 0) {
      errors_.emplace_back(error);
    } else {
      errors_.emplace_back(
          StringPrintf("%d:%d %s", line, column, error.c_str()));
    }
  };

  if (document.HasParseError()) {
    error_callback(document.GetErrorOffset(),
                   GetParseError_En(document.GetParseError()));
    return;
  }

  if (!document.IsObject()) {
    error_callback(0, "Config file is not a JSON object");
    return;
  }

  if (!(ParseServiceMap(document, kServices, &services_, error_callback) &&
        ParseServiceMap(document, kAppLoaders, &app_loaders_,
                        error_callback))) {
    return;
  }

  auto apps_it = document.FindMember(kApps);
  if (apps_it != document.MemberEnd()) {
    const auto& value = apps_it->value;
    const auto* name = apps_it->name.GetString();
    if (value.IsArray()) {
      for (const auto& app : value.GetArray()) {
        auto launch_info = GetLaunchInfo(app, name, error_callback);
        if (launch_info) {
          apps_.push_back(std::move(launch_info));
        }
      }
    } else {
      error_callback(0, StringPrintf("%s value is not an array", name));
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
      error_callback(0, StringPrintf("%s is not an array of strings", name));
    }
  }
}

}  // namespace sysmgr
