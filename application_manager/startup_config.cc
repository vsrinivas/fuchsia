// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/application_manager/startup_config.h"

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
    for (const auto& application_name : value.GetArray()) {
      if (!application_name.IsString())
        return false;
      initial_apps_.push_back(application_name.GetString());
    }
  }

  return true;
}

std::vector<std::string> StartupConfig::TakeInitialApps() {
  return std::move(initial_apps_);
}

}  // namespace modular
