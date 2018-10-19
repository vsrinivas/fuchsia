// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <memory>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/bin/zxdb/client/setting_value.h"

#include "lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class SettingSchema;
struct StoredSetting;

// SettingStore is in charge of maintaining a structured group of settings.
// settings are indexed by a unique "path". Paths are dot (.) separated paths
// that point to a particular settings (eg. "this.is.a.path").
//
// These paths creates a hierarchical structure that can then be queried and
// shown to users.
class SettingStore {
 public:
  // The store has some knowledge about what "level" it is coming from. This
  // enables us to communicate this back when we query for a value. This is
  // because a store can fallback to another stores and we need to communicate
  // to the caller that the value was overriden (and where).
  enum class Level {
    kSystem,
    kTarget,
    kThread,
    kDefault,  // Means no override, so value is the schema's default.
  };

  SettingStore(Level, fxl::RefPtr<SettingSchema> schema,
               SettingStore* fallback);

  void set_fallback(SettingStore* fallback) { fallback_ = fallback; }

  Level level() const { return level_; }

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

 private:
  // Actual function that traverses the path and creates the intermediate
  // nodes. |add_value_fn| is called to add the correct value to the newly
  // created node. These functions will be provided by the public interface
  // which is the one the user cals (eg. AddBool, AddString, etc.).

  // Adding a setting if the same, only that the value differs. This will call
  // the correct overload for the setting value and store it is valid.
  template <typename T>
  Err SetSetting(const std::string& key, T t);

  // Should always exist. All settings are validated against this.
  fxl::RefPtr<SettingSchema> schema_;

  // SettingStore this store lookup settings when it cannot find them locally.
  // Can be null. If set, should outlive |this|.
  SettingStore* fallback_;

  Level level_;

  std::map<std::string, SettingValue> settings_;
};

// Represents a value of a setting with some metadata associated to it so
// the frontend can show it.
struct StoredSetting {
  SettingValue value;
  SettingSchemaItem schema_item;
  // From what context level the value actually came from.
  SettingStore::Level level = SettingStore::Level::kDefault;
};

}  // namespace zxdg
