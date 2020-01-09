// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_store.h"

#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/zxdb/client/setting_schema.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

SettingStore::SettingStore(fxl::RefPtr<SettingSchema> schema) : schema_(std::move(schema)) {}

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

SettingValue SettingStore::GetValue(const std::string& key) const {
  const SettingSchema::Record* record = schema_->GetSetting(key);
  if (!record)
    return SettingValue();  // Not in the schema.

  SettingValue value = GetStorageValue(key);
  if (value.type() != SettingType::kNull)
    return value;

  // Not found in the storage, fall back on the default value from the schema.
  return record->default_value;
}

Err SettingStore::SetBool(const std::string& key, bool val) {
  return SetValue(key, SettingValue(val));
}

Err SettingStore::SetInt(const std::string& key, int val) {
  return SetValue(key, SettingValue(val));
}

Err SettingStore::SetString(const std::string& key, std::string val) {
  return SetValue(key, SettingValue(std::move(val)));
}

Err SettingStore::SetExecutionScope(const std::string& key, const ExecutionScope& scope) {
  return SetValue(key, SettingValue(scope));
}

Err SettingStore::SetInputLocations(const std::string& key, std::vector<InputLocation> v) {
  return SetValue(key, SettingValue(std::move(v)));
}

Err SettingStore::SetList(const std::string& key, std::vector<std::string> list) {
  return SetValue(key, SettingValue(std::move(list)));
}

Err SettingStore::SetValue(const std::string& key, SettingValue value) {
  if (Err err = schema_->ValidateSetting(key, value); err.has_error())
    return err;
  return SetStorageValue(key, value);
}

}  // namespace zxdb
