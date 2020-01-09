// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/map_setting_store.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

MapSettingStore::MapSettingStore(fxl::RefPtr<SettingSchema> schema, MapSettingStore* fallback)
    : SettingStore(std::move(schema)), fallback_(fallback) {}

void MapSettingStore::AddObserver(const std::string& setting_name, SettingStoreObserver* observer) {
  observer_map_[setting_name].AddObserver(observer);
}

void MapSettingStore::RemoveObserver(const std::string& setting_name,
                                     SettingStoreObserver* observer) {
  observer_map_[setting_name].RemoveObserver(observer);
}

void MapSettingStore::NotifySettingChanged(const std::string& setting_name) const {
  for (const auto& [key, observers] : observer_map_) {
    if (key != setting_name)
      continue;

    for (auto& observer : observers)
      observer.OnSettingChanged(*this, setting_name);
  }
}

SettingValue MapSettingStore::GetStorageValue(const std::string& key) const {
  // Explicit setting on this map.
  if (auto it = values_.find(key); it != values_.end())
    return it->second;

  // Check the fallback store.
  if (fallback_) {
    SettingValue value = fallback_->GetStorageValue(key);
    if (!value.is_null())
      return value;
  }

  // Not found in this store.
  return SettingValue();
}

Err MapSettingStore::SetStorageValue(const std::string& key, SettingValue value) {
  values_[key] = std::move(value);
  NotifySettingChanged(key);
  return Err();
}

}  // namespace zxdb
