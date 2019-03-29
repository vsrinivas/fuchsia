// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/setting_value.h"

#include <algorithm>
#include <vector>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"

namespace zxdb {

const char* SettingTypeToString(SettingType type) {
  switch (type) {
    case SettingType::kBoolean:
      return "bool";
    case SettingType::kInteger:
      return "int";
    case SettingType::kString:
      return "string";
    case SettingType::kList:
      return "list";
    case SettingType::kNull:
      return "<null>";
  }
}

SettingValue::SettingValue() = default;

SettingValue::SettingValue(bool val)
    : type_(SettingType::kBoolean), value_(val) {}

SettingValue::SettingValue(int val)
    : type_(SettingType::kInteger), value_(val) {}

SettingValue::SettingValue(const char* val)
    : type_(SettingType::kString), value_(std::string(val)) {}

SettingValue::SettingValue(std::string val)
    : type_(SettingType::kString), value_(val) {}

SettingValue::SettingValue(std::vector<std::string> val)
    : type_(SettingType::kList), value_(std::move(val)) {}

bool& SettingValue::GetBool() {
  FXL_DCHECK(type_ == SettingType::kBoolean);
  return std::get<bool>(value_);
}

bool SettingValue::GetBool() const {
  return const_cast<SettingValue*>(this)->GetBool();
}

int& SettingValue::GetInt() {
  FXL_DCHECK(type_ == SettingType::kInteger);
  return std::get<int>(value_);
}

int SettingValue::GetInt() const {
  return const_cast<SettingValue*>(this)->GetInt();
}

std::string& SettingValue::GetString() {
  FXL_DCHECK(type_ == SettingType::kString);
  return std::get<std::string>(value_);
}

const std::string& SettingValue::GetString() const {
  FXL_DCHECK(type_ == SettingType::kString);
  return std::get<std::string>(value_);
}

std::vector<std::string>& SettingValue::GetList() {
  FXL_DCHECK(type_ == SettingType::kList);
  return std::get<std::vector<std::string>>(value_);
}

const std::vector<std::string>& SettingValue::GetList() const {
  FXL_DCHECK(type_ == SettingType::kList);
  return std::get<std::vector<std::string>>(value_);
}

}  // namespace zxdb
