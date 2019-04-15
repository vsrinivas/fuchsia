// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/setting_value.h"

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
    : type(SettingType::kBoolean), value(val) {}

SettingValue::SettingValue(int val)
    : type(SettingType::kInteger), value(val) {}

SettingValue::SettingValue(const char* val)
    : type(SettingType::kString), value(std::string(val)) {}

SettingValue::SettingValue(std::string val)
    : type(SettingType::kString), value(val) {}

SettingValue::SettingValue(std::vector<std::string> val)
    : type(SettingType::kList), value(std::move(val)) {}

std::string SettingValue::ToDebugString() const {
  std::stringstream ss;
  ss << "[" << SettingTypeToString(type) << "]: ";
  switch (type) {
    case SettingType::kNull:
      return "<null>";
    case SettingType::kBoolean:
      ss << get_bool();
      break;
    case SettingType::kInteger:
      ss << get_int();
      break;
    case SettingType::kString:
      return get_string();
      break;
    case SettingType::kList:
      for (auto& v : get_list()) {
        ss << v << ", ";
      }
      break;
  }

  return ss.str();
}

}  // namespace zxdb
