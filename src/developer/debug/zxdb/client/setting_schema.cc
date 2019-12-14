// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_schema.h"

#include <algorithm>
#include <set>
#include <string>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

namespace {

Err ValidateOptions(const std::vector<std::string>& options, const std::vector<std::string>& set) {
  for (auto& s : set) {
    if (std::find(options.begin(), options.end(), s) == options.end())
      return Err("Option \"%s\" is not a valid option", s.c_str());
  }
  return Err();
}

}  // namespace

void SettingSchema::AddBool(std::string name, std::string description, bool v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

void SettingSchema::AddInt(std::string name, std::string description, int v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

void SettingSchema::AddExecutionScope(std::string name, std::string description,
                                      const ExecutionScope v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

void SettingSchema::AddInputLocations(std::string name, std::string description,
                                      std::vector<InputLocation> v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(std::move(v))});
}

void SettingSchema::AddString(std::string name, std::string description, std::string v,
                              std::vector<std::string> valid_options) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)}, std::move(valid_options));
}

bool SettingSchema::AddList(std::string name, std::string description, std::vector<std::string> v,
                            std::vector<std::string> options) {
  if (!options.empty() && ValidateOptions(options, v).has_error())
    return false;

  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)}, std::move(options));

  return true;
}

void SettingSchema::AddSetting(const std::string& key, Setting setting,
                               std::vector<std::string> options) {
  auto& schema_setting = settings_[key];
  schema_setting.setting = std::move(setting);
  schema_setting.options = std::move(options);
}

bool SettingSchema::HasSetting(const std::string& key) {
  return settings_.find(key) != settings_.end();
}

Err SettingSchema::ValidateSetting(const std::string& key, const SettingValue& value) const {
  auto it = settings_.find(key);
  if (it == settings_.end())
    return Err("Setting \"%s\" not found in the given context.", key.data());

  auto& setting = it->second;
  if (setting.setting.value.type() != value.type()) {
    return Err("Setting \"%s\" expects a different type (expected: %s, given: %s).", key.data(),
               SettingTypeToString(value.type()),
               SettingTypeToString(setting.setting.value.type()));
  }

  if (!setting.options.empty()) {
    // Validate the setting value.
    if (setting.setting.value.is_list()) {
      // Each list element must be in the valid option list.
      if (Err err = ValidateOptions(setting.options, value.get_list()); err.has_error())
        return err;
    } else if (setting.setting.value.is_string()) {
      // String must be in the valid option list.
      if (std::find(setting.options.begin(), setting.options.end(), value.get_string()) ==
          setting.options.end()) {
        return Err("Option \"%s\" is not a valid option", value.get_string().c_str());
      }
    }
  }

  return Err();
}

const SettingSchema::SchemaSetting& SettingSchema::GetSetting(const std::string& name) const {
  static SchemaSetting null_setting;

  const auto& setting = settings_.find(name);
  if (setting == settings_.end())
    return null_setting;
  return setting->second;
}

}  // namespace zxdb
