// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/bin/zxdb/client/setting_store_observer.h"
#include "garnet/bin/zxdb/client/setting_value.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/observer_list.h"
#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

struct StoredSetting;

// SettingStore is in charge of maintaining a structured group of settings.
// settings are indexed by a unique key.
class SettingStore {
 public:
  SettingStore(fxl::RefPtr<SettingSchema> schema, SettingStore* fallback);

  SettingStore* fallback() const { return fallback_; }
  void set_fallback(SettingStore* fallback) { fallback_ = fallback; }

  fxl::RefPtr<SettingSchema> schema() const { return schema_; }

  void AddObserver(const std::string& setting_name, SettingStoreObserver*);
  void RemoveObserver(const std::string& setting_name, SettingStoreObserver*);

  // What level is this store associated with.
  SettingSchema::Level level() const { return schema_->level(); }

  Err SetBool(const std::string& key, bool);
  Err SetInt(const std::string& key, int);
  Err SetString(const std::string& key, std::string);
  Err SetList(const std::string& key, std::vector<std::string> list);

  bool GetBool(const std::string& key) const;
  int GetInt(const std::string& key) const;
  std::string GetString(const std::string& key) const;
  std::vector<std::string> GetList(const std::string& key) const;

  // Normally we know zxdb defined setting types, so we can confidently used the
  // type getters. But frontend code might want to check for dynamically defined
  // settings and check their type.
  //
  // |return_default| specifies whether the call should return the schema's
  // default value. This is needed because this SettingStore will call a
  // fallback for its value "recursively", so we need to tell _that_ store not
  // to return its default value because it belongs to another schema.
  //
  // Returns a null setting if the key is not found.
  StoredSetting GetSetting(const std::string& key,
                           bool return_default = true) const;
  std::map<std::string, StoredSetting> GetSettings() const;

  bool HasSetting(const std::string& key) const;

 protected:
  std::map<std::string, fxl::ObserverList<SettingStoreObserver>>& observers() {
    return observer_map_;
  }

 private:
  // Actual function that traverses the path and creates the intermediate
  // nodes. |add_value_fn| is called to add the correct value to the newly
  // created node. These functions will be provided by the public interface
  // which is the one the user cals (eg. AddBool, AddString, etc.).

  // Adding a setting if the same, only that the value differs. This will call
  // the correct overload for the setting value and store it is valid.
  template <typename T>
  Err SetSetting(const std::string& key, T t);

  void NotifySettingChanged(const std::string& setting_name) const;

  // Should always exist. All settings are validated against this.
  fxl::RefPtr<SettingSchema> schema_;

  // SettingStore this store lookup settings when it cannot find them locally.
  // Can be null. If set, should outlive |this|.
  SettingStore* fallback_;
  std::map<std::string, SettingValue> settings_;

  std::map<std::string, fxl::ObserverList<SettingStoreObserver>> observer_map_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SettingStore);
};

// Represents a value of a setting with some metadata associated to it so
// the frontend can show it.
struct StoredSetting {
  SettingValue value;
  SettingSchemaItem schema_item;
  // From what context level the value actually came from.
  SettingSchema::Level level = SettingSchema::Level::kDefault;
};

}  // namespace zxdb
