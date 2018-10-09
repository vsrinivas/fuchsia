// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting_store.h"

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "lib/fxl/logging.h"

namespace zxdb {

SettingStore::SettingStore(fxl::RefPtr<SettingSchema> schema,
                           SettingStore* fallback)
    : schema_(std::move(schema)), fallback_(fallback) {}

// Getters ---------------------------------------------------------------------

bool SettingStore::GetBool(const std::string& key) const {
  SettingValue setting = GetSetting(key);
  FXL_DCHECK(setting.is_bool());
  return setting.GetBool();
}

int SettingStore::GetInt(const std::string& key) const {
  SettingValue setting = GetSetting(key);
  FXL_DCHECK(setting.is_int());
  return setting.GetInt();
}

std::string SettingStore::GetString(const std::string& key) const {
  SettingValue setting = GetSetting(key);
  FXL_DCHECK(setting.is_string());
  return setting.GetString();
}

std::vector<std::string> SettingStore::GetList(const std::string& key) const {
  SettingValue setting = GetSetting(key);
  FXL_DCHECK(setting.is_list());
  return setting.GetList();
}

SettingValue SettingStore::GetSetting(const std::string& key) const {
  // Check if it already exists. If so, we know that is within this schema.
  auto it = settings_.find(key);
  if (it != settings_.end())
    return it->second;

  // Before checking the callback, we want to know if the option is actually
  // defined.
  if (!schema_->HasSetting(key))
    return SettingValue();

  // We check the fallback SettingStore to see if it has the setting.
  if (fallback_)
    return fallback_->GetSetting(key);

  // Return the default value defined by the schema.
  return schema_->GetDefault(key);
}

// Setters ---------------------------------------------------------------------

Err SettingStore::SetBool(const std::string& key, bool val) {
  return SetSetting(key, val);
}

Err SettingStore::SetInt(const std::string& key, int val) {
  return SetSetting(key, val);
}

Err SettingStore::SetString(const std::string& key, std::string val) {
  return SetSetting(key, std::move(val));
}

Err SettingStore::SetList(const std::string& key,
                          std::vector<std::string> list) {
  return SetSetting(key, std::move(list));
}

template <typename T>
Err SettingStore::SetSetting(const std::string& key, T t) {
  // Check if the setting is valid.
  SettingValue setting(t);
  Err err = schema_->ValidateSetting(key, setting);
  if (err.has_error())
    return err;

  // We can safely insert or override.
  settings_[key] = SettingValue(std::move(t));
  return Err();
}

}  // namespace zxdb
