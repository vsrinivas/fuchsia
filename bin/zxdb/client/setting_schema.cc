// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting_schema.h"
#include "garnet/public/lib/fxl/strings/join_strings.h"

namespace zxdb {

namespace {

bool StringWithinOptions(const std::string& str,
                         const std::vector<std::string>& options) {
  return options.empty() ||
         std::find(options.begin(), options.end(), str) != options.end();
}

Err StringNotWithinOptionsError(const std::string& value,
                                const std::vector<std::string>& options) {
  std::string options_str = fxl::JoinStrings(options, ", ");
  return Err("Value %s is not within the valid values: [%s]", value.data(),
             options_str.data());
}

}  // namespace

// SettingSchemaItem -----------------------------------------------------------

SettingSchemaItem::SettingSchemaItem() = default;

// Special case for valid options.
SettingSchemaItem SettingSchemaItem::StringWithOptions(
    std::string name, std::string description, std::string value,
    std::vector<std::string> valid_values) {
  // Validate that the value is within the options.
  if (!StringWithinOptions(value, valid_values))
    return SettingSchemaItem();

  SettingSchemaItem item(std::move(name), std::move(description),
                         std::move(value));
  item.valid_values_ = std::move(valid_values);
  return item;
}

// SettingSchema ---------------------------------------------------------------

void SettingSchema::AddBool(const std::string& name, std::string description,
                            bool value) {
  auto item = SettingSchemaItem(name, std::move(description), value);
  AddSetting(std::move(name), std::move(item));
}

void SettingSchema::AddInt(const std::string& name, std::string description,
                            int value) {
  auto item = SettingSchemaItem(name, std::move(description), value);
  AddSetting(std::move(name), std::move(item));
}

void SettingSchema::AddString(const std::string& name, std::string description,
                              std::string value,
                              std::vector<std::string> valid_values) {
  auto item = SettingSchemaItem::StringWithOptions(
      name, std::move(description), std::move(value), std::move(valid_values));
  AddSetting(std::move(name), std::move(item));
}

void SettingSchema::AddList(const std::string& name, std::string description,
                            std::vector<std::string> list) {
  auto item = SettingSchemaItem(name, std::move(description), std::move(list));
  AddSetting(std::move(name), std::move(item));
}

void SettingSchema::AddSetting(const std::string& key, SettingSchemaItem item) {
  items_[key] = std::move(item);
}

bool SettingSchema::HasSetting(const std::string& key) {
  return items_.find(key) != items_.end();
}

Err SettingSchema::ValidateSetting(const std::string& key,
                                   const SettingValue& value) const {
  auto it = items_.find(key);
  if (it == items_.end())
    return Err("Setting \"%s\" not found", key.data());

  auto& schema_item = it->second;
  if (schema_item.type() != value.type())
    return Err("Setting \"%s\" expects a different type (%s vs %s given)",
               key.data(), SettingTypeToString(value.type()),
               SettingTypeToString(schema_item.type()));

  if (value.is_string() &&
      !StringWithinOptions(value.GetString(), schema_item.valid_values()))
    return StringNotWithinOptionsError(key, schema_item.valid_values());

  return Err();
}

SettingSchemaItem SettingSchema::GetItem(const std::string& name) const {
  const auto& item = items_.find(name);
  if (item == items_.end())
    return SettingSchemaItem();
  return item->second;
}

SettingValue SettingSchema::GetDefault(const std::string& key) const {
  auto it = items_.find(key);
  FXL_DCHECK(it != items_.end());
  return it->second.value();
}

}  // namespace zxdb
