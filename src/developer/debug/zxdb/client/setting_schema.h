// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "src/developer/debug/zxdb/client/setting_value.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class SettingSchemaItem;

// Stores the setting information for a particular context. These are meant
// to be used for validation of settings for particular objects (thread,
// process, etc.).
class SettingSchema : public fxl::RefCountedThreadSafe<SettingSchema> {
 public:
  // The schema has some knowledge about what "level" it is coming from. This
  // enables us to communicate this back when we query for a value. This is
  // because a store can fallback to another stores and we need to communicate
  // to the caller that the value was overriden (and where).
  enum class Level {
    kSystem,
    kJob,
    kTarget,
    kThread,
    kDefault,  // Means no override, so value is the schema's default.
  };
  static const char* LevelToString(Level);

  explicit SettingSchema(Level);

  Level level() const { return level_; }

  bool HasSetting(const std::string& key);
  Err ValidateSetting(const std::string& key, const SettingValue&) const;

  // Create new items for settings that only belong to this schema.
  // For inter-schema options, the easier way is to create the SettingSchemaItem
  // separately and then insert it to each schema with AddSetting.
  //
  // |overriden| marks whether this option is meant to be an override for
  // another schema. This enables the frontend to only list the setting once in
  // the correct schema section.
  void AddBool(const std::string& name, std::string description,
               bool value = false, bool overriden = false);
  void AddInt(const std::string& name, std::string description, int value = 0,
              bool overriden = false);
  void AddString(const std::string& name, std::string description,
                 std::string value = {},
                 std::vector<std::string> valid_values = {},
                 bool overriden = false);
  void AddList(const std::string& name, std::string description,
               std::vector<std::string> list = {}, bool overriden = false);

  // Use for inserting a previously created setting.
  void AddSetting(const std::string& key, SettingSchemaItem item,
                  bool overriden = false);

  // For use of SettingStore. Will assert if the key is not found.
  SettingValue GetDefault(const std::string& key) const;

  SettingSchemaItem GetItem(const std::string& name) const;

  const std::map<std::string, SettingSchemaItem>& items() const {
    return items_;
  }

 private:
  std::map<std::string, SettingSchemaItem> items_;
  Level level_;
};

// Holds the metadata and default value for a setting.
class SettingSchemaItem {
 public:
  // Returns a null item. Should not be inserted to a schema.
  SettingSchemaItem();

  // The type will be implicitly known by the correct constructor of
  // SettingValue.
  // |overriden| indicates whether this option is meant to be an override. eg.
  // Target defines a setting for system in order to be able to override it.
  // We set this flag so that when we list the settings, we don't repeat the
  // setting for both system and process.
  template <typename T>
  SettingSchemaItem(std::string name, std::string description, T default_value,
                    bool overriden = false)
      : name_(std::move(name)),
        description_(description),
        default_value_(std::move(default_value)),
        overriden_(overriden) {}

  // Special case for adding valid options to a string.
  // If there are no options to filter with (|valid_values| is empty), any value
  // if allowed.
  //
  // If the value given is not within the options, it will return a null-typed
  // SettingSchemaItem.
  static SettingSchemaItem StringWithOptions(
      std::string name, std::string description, std::string value,
      std::vector<std::string> valid_values = {}, bool overriden = false);

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }
  bool overriden() const { return overriden_; }
  void set_overriden(bool overriden) { overriden_ = overriden; }

  SettingType type() const { return default_value_.type; }
  const SettingValue& value() const { return default_value_; }

  const std::vector<std::string>& valid_values() const { return valid_values_; }

 private:
  std::string name_;
  std::string description_;
  SettingValue default_value_;
  // Only used for strings with options.
  std::vector<std::string> valid_values_;
  bool overriden_ = false;
};

}  // namespace zxdb
