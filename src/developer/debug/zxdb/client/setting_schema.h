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
  bool HasSetting(const std::string& key);

  // Returns a null setting if |key| is not within the schema.
  Setting GetSetting(const std::string& name) const;
  void AddSetting(const std::string& key, Setting setting);

  // Create new items for settings that only belong to this schema.
  // For inter-schema options, the easier way is to create the Setting
  // separately and then insert it to each schema with AddSetting.
  void AddBool(std::string name, std::string description, bool value = false);
  void AddInt(std::string name, std::string description, int value = 0);
  void AddString(std::string name, std::string description,
                 std::string value = {});
  void AddList(std::string name, std::string description,
               std::vector<std::string> list = {});

  Err ValidateSetting(const std::string& key, const SettingValue&) const;

  const std::map<std::string, Setting>& settings() const { return settings_; }

  bool empty() const { return settings_.empty(); }

 private:
  std::map<std::string, Setting> settings_;
};

}  // namespace zxdb
