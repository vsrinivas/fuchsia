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

void TrimAndLowerCase(std::vector<std::string>* strings) {
  for (std::string& s : *strings) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    s = fxl::TrimString(s, " ").ToString();
  }
}

bool IsSuperSet(const std::vector<std::string>& super_set,
                const std::vector<std::string>& set) {
  std::set<std::string> sset;
  sset.insert(super_set.begin(), super_set.end());
  for (auto& s : set) {
    auto [_, inserted] = sset.insert(s);
    // If we insert a new string, means that it's a new one.
    if (inserted)
      return false;
  }

  return true;
}

}  // namespace

void SettingSchema::AddBool(std::string name, std::string description,
                            bool v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

void SettingSchema::AddInt(std::string name, std::string description, int v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

void SettingSchema::AddString(std::string name, std::string description,
                              std::string v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

bool SettingSchema::AddList(std::string name, std::string description,
                            std::vector<std::string> v,
                            std::vector<std::string> options) {
  // Transform everything to lower case.
  TrimAndLowerCase(&v);
  TrimAndLowerCase(&options);
  if (!options.empty() && !IsSuperSet(options, v))
    return false;

  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)},
             std::move(options));

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

Err SettingSchema::ValidateSetting(const std::string& key,
                                   const SettingValue& value) const {
  auto it = settings_.find(key);
  if (it == settings_.end())
    return Err("Setting \"%s\" not found in the given context.", key.data());

  auto& setting = it->second;
  if (setting.setting.value.type != value.type) {
    return Err(
        "Setting \"%s\" expects a different type (expected: %s, given: %s).",
        key.data(), SettingTypeToString(value.type),
        SettingTypeToString(setting.setting.value.type));
  }

  return Err();
}

const SettingSchema::SchemaSetting& SettingSchema::GetSetting(
    const std::string& name) const {
  static SchemaSetting null_setting;

  const auto& setting = settings_.find(name);
  if (setting == settings_.end())
    return null_setting;
  return setting->second;
}

}  // namespace zxdb
