// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_VALUE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_VALUE_H_

#include <string>
#include <variant>
#include <vector>

#include "src/developer/debug/zxdb/client/execution_scope.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

enum class SettingType : uint32_t {
  kBoolean = 0,
  kInteger,
  kString,
  kList,
  kExecutionScope,
  kInputLocations,
  kNull,
};
const char* SettingTypeToString(SettingType);

class SettingValue {
 public:
  SettingValue();  // Creates a kNull type.
  explicit SettingValue(bool);
  explicit SettingValue(int);
  explicit SettingValue(const char*);
  explicit SettingValue(std::string);
  explicit SettingValue(std::vector<std::string>);
  explicit SettingValue(ExecutionScope);
  explicit SettingValue(std::vector<InputLocation>);

  SettingType type() const { return type_; }

  bool is_bool() const { return type_ == SettingType::kBoolean; }
  bool is_int() const { return type_ == SettingType::kInteger; }
  bool is_string() const { return type_ == SettingType::kString; }
  bool is_list() const { return type_ == SettingType::kList; }
  bool is_execution_scope() const { return type_ == SettingType::kExecutionScope; }
  bool is_input_locations() const { return type_ == SettingType::kInputLocations; }
  bool is_null() const { return type_ == SettingType::kNull; }

  const auto& get_bool() const { return std::get<bool>(value_); }
  const auto& get_int() const { return std::get<int>(value_); }
  const auto& get_string() const { return std::get<std::string>(value_); }
  const auto& get_list() const { return std::get<std::vector<std::string>>(value_); }
  const auto& get_execution_scope() const { return std::get<ExecutionScope>(value_); }
  const auto& get_input_locations() const { return std::get<std::vector<InputLocation>>(value_); }

  void set_bool(bool v) { value_ = v; }
  void set_int(int v) { value_ = v; }
  void set_string(std::string v) { value_ = std::move(v); }
  void set_list(std::vector<std::string> v) { value_ = std::move(v); }
  void set_execution_scope(const ExecutionScope& s) { value_ = s; }
  void set_input_locations(std::vector<InputLocation> v) { value_ = std::move(v); }

  std::string ToDebugString() const;

  using VariantValue = std::variant<bool, int, std::string, std::vector<std::string>,
                                    ExecutionScope, std::vector<InputLocation>>;

 private:
  SettingType type_ = SettingType::kNull;
  VariantValue value_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_CLIENT_SETTING_VALUE_H_
