// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting_store.h"

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

StoredSetting CreateStoredSetting(SettingValue value, SettingSchemaItem item,
                                  SettingSchema::Level level) {
  StoredSetting setting = {};
  setting.value = std::move(value);
  setting.schema_item = std::move(item);
  setting.level = level;
  return setting;
}

}  // namespace

SettingStore::SettingStore(fxl::RefPtr<SettingSchema> schema,
                           SettingStore* fallback)
    : schema_(std::move(schema)), fallback_(fallback) {}

void SettingStore::AddObserver(const std::string& setting_name,
                               SettingStoreObserver* observer) {
  observer_map_[setting_name].AddObserver(observer);
}

void SettingStore::RemoveObserver(const std::string& setting_name,
                                  SettingStoreObserver* observer) {
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

// Getters ---------------------------------------------------------------------

bool SettingStore::GetBool(const std::string& key) const {
  auto setting = GetSetting(key);
  FXL_DCHECK(setting.value.is_bool());
  return setting.value.GetBool();
}

int SettingStore::GetInt(const std::string& key) const {
  auto setting = GetSetting(key);
  FXL_DCHECK(setting.value.is_int());
  return setting.value.GetInt();
}

std::string SettingStore::GetString(const std::string& key) const {
  auto setting = GetSetting(key);
  FXL_DCHECK(setting.value.is_string());
  return setting.value.GetString();
}

std::vector<std::string> SettingStore::GetList(const std::string& key) const {
  auto setting = GetSetting(key);
  FXL_DCHECK(setting.value.is_list());
  return setting.value.GetList();
}

StoredSetting SettingStore::GetSetting(const std::string& key,
                                       bool return_default) const {

  // Check if it already exists. If so, return it.
  auto it = settings_.find(key);
  if (it != settings_.end()) {
    // We found it. We check to see if is within the schema.
    auto schema_item = schema_->GetItem(key);
    if (schema_item.value().is_null())
      return StoredSetting();
    return CreateStoredSetting(it->second, std::move(schema_item), level());
  }

  // We check the fallback SettingStore to see if it has the setting.
  StoredSetting setting;
  if (fallback_) {
    // We tell the fallback store not return its default schema value.
    setting = fallback_->GetSetting(key, false);
    if (!setting.value.is_null())
      return setting;
  }

  // None of our fallbacks have this setting, so we check to see if it's within
  // our schema.
  auto schema_item = schema_->GetItem(key);
  if (schema_item.value().is_null())
    return StoredSetting();

  // We return the schema value only if we were told to.
  if (!return_default)
    return StoredSetting();
  return CreateStoredSetting(schema_item.value(), schema_item,
                             SettingSchema::Level::kDefault);
}

std::map<std::string, StoredSetting> SettingStore::GetSettings() const {
  std::map<std::string, StoredSetting> stored_settings;

  // We iterate over the schema looking for values.
  for (const auto& [key, schema_item] : schema_->items()) {
    StoredSetting setting = GetSetting(key);
    // There should always be a value, at least the default one.
    FXL_DCHECK(!setting.value.is_null());
    stored_settings[key] = std::move(setting);
  }
  return stored_settings;
}

bool SettingStore::HasSetting(const std::string& key) const {
  return schema_->HasSetting(key);
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

  // We can safely insert or override and notify observers.
  settings_[key] = SettingValue(std::move(t));
  NotifySettingChanged(key);

  return Err();
}

}  // namespace zxdb
