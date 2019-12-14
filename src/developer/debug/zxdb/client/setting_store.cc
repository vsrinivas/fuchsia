// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_store.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

SettingStore::SettingStore(fxl::RefPtr<SettingSchema> schema, SettingStore* fallback)
    : schema_(std::move(schema)), fallback_(fallback) {}

void SettingStore::AddObserver(const std::string& setting_name, SettingStoreObserver* observer) {
  observer_map_[setting_name].AddObserver(observer);
}

void SettingStore::RemoveObserver(const std::string& setting_name, SettingStoreObserver* observer) {
  observer_map_[setting_name].RemoveObserver(observer);
}

void SettingStore::NotifySettingChanged(const std::string& setting_name) const {
  for (const auto& [key, observers] : observer_map_) {
    if (key != setting_name)
      continue;

    for (auto& observer : observers)
      observer.OnSettingChanged(*this, setting_name);
  }
}

// Getters -----------------------------------------------------------------------------------------

bool SettingStore::GetBool(const std::string& key) const {
  auto value = GetValue(key);
  FXL_DCHECK(value.is_bool());
  return value.get_bool();
}

int SettingStore::GetInt(const std::string& key) const {
  auto value = GetValue(key);
  FXL_DCHECK(value.is_int());
  return value.get_int();
}

std::string SettingStore::GetString(const std::string& key) const {
  auto value = GetValue(key);
  FXL_DCHECK(value.is_string());
  return value.get_string();
}

const ExecutionScope& SettingStore::GetExecutionScope(const std::string& key) const {
  auto value = GetValue(key);
  FXL_DCHECK(value.is_execution_scope());
  return value.get_execution_scope();
}

const std::vector<InputLocation>& SettingStore::GetInputLocations(const std::string& key) const {
  auto value = GetValue(key);
  FXL_DCHECK(value.is_input_locations());
  return value.get_input_locations();
}

std::vector<std::string> SettingStore::GetList(const std::string& key) const {
  auto value = GetValue(key);
  FXL_DCHECK(value.is_list());
  return value.get_list();
}

SettingValue SettingStore::GetValue(const std::string& key) const { return GetSetting(key).value; }

Setting SettingStore::GetSetting(const std::string& key) const {
  // First check if it's in the schema.
  auto default_setting = schema_->GetSetting(key);
  if (default_setting.setting.value.is_null()) {
    DEBUG_LOG(Setting) << "Store: " << name_ << ": Key not found: " << key;
    return Setting();
  }

  // Check if it already exists. If so, return it.
  auto it = values_.find(key);
  if (it != values_.end()) {
    return {std::move(default_setting.setting.info), it->second};
  }

  // We check the fallback SettingStore to see if it has the setting.
  if (fallback_) {
    DEBUG_LOG(Setting) << "Store: " << name_ << ": Going to fallback.";
    auto setting = fallback_->GetSetting(key);
    if (!setting.value.is_null())
      return setting;
  }

  // No fallback has the schema, we return the default.
  return default_setting.setting;
}

bool SettingStore::HasSetting(const std::string& key) const { return schema_->HasSetting(key); }

// Setters -----------------------------------------------------------------------------------------

Err SettingStore::SetBool(const std::string& key, bool val) { return SetSetting(key, val); }

Err SettingStore::SetInt(const std::string& key, int val) { return SetSetting(key, val); }

Err SettingStore::SetString(const std::string& key, std::string val) {
  return SetSetting(key, std::move(val));
}

Err SettingStore::SetExecutionScope(const std::string& key, const ExecutionScope& scope) {
  return SetSetting(key, scope);
}

Err SettingStore::SetInputLocations(const std::string& key, std::vector<InputLocation> v) {
  return SetSetting(key, std::move(v));
}

Err SettingStore::SetList(const std::string& key, std::vector<std::string> list) {
  return SetSetting(key, std::move(list));
}

template <typename T>
Err SettingStore::SetSetting(const std::string& key, T t) {
  // Check if the setting is valid.
  SettingValue setting(t);
  if (Err err = schema_->ValidateSetting(key, setting); err.has_error())
    return err;

  // We can safely insert or override and notify observers.
  auto new_setting = SettingValue(std::move(t));
  values_[key] = std::move(new_setting);
  NotifySettingChanged(key);

  return Err();
}

}  // namespace zxdb
