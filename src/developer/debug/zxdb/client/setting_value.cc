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
    case SettingType::kExecutionScope:
      return "scope";
    case SettingType::kNull:
      return "<null>";
  }
}

SettingValue::SettingValue() = default;

SettingValue::SettingValue(bool val) : type_(SettingType::kBoolean), value_(val) {}

SettingValue::SettingValue(int val) : type_(SettingType::kInteger), value_(val) {}

SettingValue::SettingValue(const char* val)
    : type_(SettingType::kString), value_(std::string(val)) {}

SettingValue::SettingValue(std::string val) : type_(SettingType::kString), value_(val) {}

SettingValue::SettingValue(std::vector<std::string> val)
    : type_(SettingType::kList), value_(std::move(val)) {}

SettingValue::SettingValue(ExecutionScope scope)
    : type_(SettingType::kExecutionScope), value_(scope) {}

std::string SettingValue::ToDebugString() const {
  std::stringstream ss;
  ss << "[" << SettingTypeToString(type_) << "]: ";
  switch (type_) {
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
    case SettingType::kExecutionScope:
      // Scope formatting depends on the frontend. Currently we don't have a client-agnostic
      // formatting for this.
      return "<execution scope>";
    case SettingType::kList:
      for (auto& v : get_list()) {
        ss << v << ", ";
      }
      break;
  }

  return ss.str();
}

}  // namespace zxdb
