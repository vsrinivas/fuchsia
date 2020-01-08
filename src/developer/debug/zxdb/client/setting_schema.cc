// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_schema.h"

#include <algorithm>
#include <set>
#include <string>

#include "src/lib/fxl/strings/join_strings.h"
#include "src/lib/fxl/strings/trim.h"

namespace zxdb {

namespace {

Err ValidateOptions(const std::vector<std::string>& options, const std::vector<std::string>& set) {
  for (auto& s : set) {
    if (std::find(options.begin(), options.end(), s) == options.end())
      return Err("Option \"%s\" is not a valid option", s.c_str());
  }
  return Err();
}

}  // namespace

void SettingSchema::AddBool(std::string name, std::string description, bool v) {
  AddSetting(std::move(name), std::move(description), SettingValue(v));
}

void SettingSchema::AddInt(std::string name, std::string description, int v) {
  AddSetting(std::move(name), std::move(description), SettingValue(v));
}

void SettingSchema::AddExecutionScope(std::string name, std::string description,
                                      const ExecutionScope v) {
  AddSetting(std::move(name), std::move(description), SettingValue(v));
}

void SettingSchema::AddInputLocations(std::string name, std::string description,
                                      std::vector<InputLocation> v) {
  AddSetting(std::move(name), std::move(description), SettingValue(std::move(v)));
}

void SettingSchema::AddString(std::string name, std::string description, std::string v,
                              std::vector<std::string> valid_options) {
  AddSetting(std::move(name), std::move(description), SettingValue(v), std::move(valid_options));
}

bool SettingSchema::AddList(std::string name, std::string description, std::vector<std::string> v,
                            std::vector<std::string> options) {
  if (!options.empty() && ValidateOptions(options, v).has_error())
    return false;
  AddSetting(std::move(name), std::move(description), SettingValue(v), std::move(options));
  return true;
}

void SettingSchema::AddSetting(std::string name, std::string description,
                               SettingValue default_value, std::vector<std::string> options) {
  auto& record = settings_[name];
  record.name = std::move(name);
  record.description = std::move(description);
  record.default_value = std::move(default_value);
  record.options = std::move(options);
}

bool SettingSchema::HasSetting(const std::string& key) {
  return settings_.find(key) != settings_.end();
}

Err SettingSchema::ValidateSetting(const std::string& key, const SettingValue& value) const {
  auto it = settings_.find(key);
  if (it == settings_.end())
    return Err("Setting \"%s\" not found in the given context.", key.data());

  const auto& record = it->second;
  if (record.default_value.type() != value.type()) {
    return Err("Setting \"%s\" expects a different type (expected: %s, given: %s).", key.c_str(),
               SettingTypeToString(record.default_value.type()), SettingTypeToString(value.type()));
  }

  if (!record.options.empty()) {
    // Validate the setting value.
    if (value.is_list()) {
      // Each list element must be in the valid option list.
      if (Err err = ValidateOptions(record.options, value.get_list()); err.has_error())
        return err;
    } else if (value.is_string()) {
      // String must be in the valid option list.
      if (std::find(record.options.begin(), record.options.end(), value.get_string()) ==
          record.options.end()) {
        return Err("Option \"%s\" is not a valid option", value.get_string().c_str());
      }
    }
  }

  return Err();
}

const SettingSchema::Record* SettingSchema::GetSetting(const std::string& name) const {
  const auto& found = settings_.find(name);
  if (found == settings_.end())
    return nullptr;
  return &found->second;
}

}  // namespace zxdb
