// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/client/setting_value.h"
#include "garnet/public/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class SettingSchemaItem;

// Stores the setting information for a particular context. These are meant
// to be used for validation of settings for particular objects (thread,
// process, etc.).
class SettingSchema : public fxl::RefCountedThreadSafe<SettingSchema> {
 public:
  bool HasSetting(const std::string& key);
  Err ValidateSetting(const std::string& key, const SettingValue&) const;

  // Create new items for settings that only belong to this schema.
  // For inter-schema options, the easier way is to create the SettingSchemaItem
  // separatedly and then insert it to each schema with AddSetting.
  void AddBool(const std::string& name, std::string description,
               bool value = false);
  void AddInt(const std::string& name, std::string description, int value = 0);
  void AddString(const std::string& name, std::string description,
                 std::string value = {},
                 std::vector<std::string> valid_values = {});
  void AddList(const std::string& name, std::string description,
               std::vector<std::string> list = {});

  // Use for inserting a previously created setting.
  void AddSetting(const std::string& key, SettingSchemaItem item);

  // For use of SettingStore. Will assert if the key is not found.
  SettingValue GetDefault(const std::string& key) const;

  const std::map<std::string, SettingSchemaItem>& items() const {
    return items_;
  }

 private:
  std::map<std::string, SettingSchemaItem> items_;
};

// Holds the metadata and default value for a setting.
class SettingSchemaItem {
 public:
  // Returns a null item. Should not be inserted to a schema.
  SettingSchemaItem();

  // The type will be implicitly known by the correct constructor of
  // SettingValue.
  template <typename T>
  SettingSchemaItem(std::string name, std::string description, T default_value)
      : name_(std::move(name)),
        description_(description),
        default_value_(std::move(default_value)) {}

  // Special case for adding valid options to a string.
  // If there are no options to filter with (|valid_values| is empty), any value
  // if allowed.
  //
  // If the value given is not within the options, it will return a null-typed
  // SettingSchemaItem.
  static SettingSchemaItem StringWithOptions(
      std::string name, std::string description, std::string value,
      std::vector<std::string> valid_values = {});

  const std::string& name() const { return name_; }
  const std::string& description() const { return description_; }

  SettingType type() const { return default_value_.type(); }
  const SettingValue& value() const { return default_value_; }

  const std::vector<std::string>& valid_values() const { return valid_values_; }

 private:
  std::string name_;
  std::string description_;
  SettingValue default_value_;
  // Only used for strings with options.
  std::vector<std::string> valid_values_;
};

}  // namespace zxdb
