// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>

#include "src/developer/debug/zxdb/client/setting_value.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

// Stores the setting information for a particular context. These are meant
// to be used for validation of settings for particular objects (thread,
// process, etc.).
class SettingSchema : public fxl::RefCountedThreadSafe<SettingSchema> {
 public:
  // The SchemaSetting holds the actual setting (the value that is stored and
  // overriden by SettingStore) + some metadata useful for implementing more
  // complex settings such as enums, by using the |options| field.
  struct SchemaSetting {
    Setting setting;
    std::vector<std::string> options; // Used only for string lists.
  };

  bool HasSetting(const std::string& key);

  bool empty() const { return settings_.empty(); }

  // Returns a null setting if |key| is not within the schema.
  const SchemaSetting& GetSetting(const std::string& name) const;
  const std::map<std::string, SchemaSetting>& settings() const {
    return settings_;
  }

  // Create new items for settings that only belong to this schema.
  // For inter-schema options, the easier way is to create the Setting
  // separately and then insert it to each schema with AddSetting.
  void AddBool(std::string name, std::string description, bool value = false);
  void AddInt(std::string name, std::string description, int value = 0);
  void AddString(std::string name, std::string description,
                 std::string value = {});

  // |valid_options| determines which options will be accepted when writing into
  // a setting. They will be stored as lowercase and comparison will be in
  // lowercase for simplicty.
  // Will return false if the given list has a entry that is not within the
  // valid options.
  bool AddList(std::string name, std::string description,
               std::vector<std::string> list = {},
               std::vector<std::string> valid_options = {});

  // |options| are used to implement enum-like strings/lists.
  void AddSetting(const std::string& key, Setting setting,
                  std::vector<std::string> options = {});

  Err ValidateSetting(const std::string& key, const SettingValue&) const;

 private:
  std::map<std::string, SchemaSetting> settings_;
};

}  // namespace zxdb
