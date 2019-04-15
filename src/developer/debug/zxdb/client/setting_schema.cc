// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/lib/fxl/strings/join_strings.h"

namespace zxdb {

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

void SettingSchema::AddList(std::string name, std::string description,
                            std::vector<std::string> v) {
  SettingInfo info{name, std::move(description)};
  AddSetting(std::move(name), {std::move(info), SettingValue(v)});
}

void SettingSchema::AddSetting(const std::string& key, Setting item) {
  settings_[key] = std::move(item);
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
  if (setting.value.type != value.type) {
    return Err(
        "Setting \"%s\" expects a different type (expected: %s, given: %s).",
        key.data(), SettingTypeToString(value.type),
        SettingTypeToString(setting.value.type));
  }

  return Err();
}

Setting SettingSchema::GetSetting(const std::string& name) const {
  const auto& setting = settings_.find(name);
  if (setting == settings_.end())
    return Setting();
  return setting->second;
}

}  // namespace zxdb
