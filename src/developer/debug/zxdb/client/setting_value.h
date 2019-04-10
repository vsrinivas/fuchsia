// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <variant>
#include <vector>

namespace zxdb {

enum class SettingType : uint32_t {
  kBoolean = 0,
  kInteger,
  kString,
  kList,
  kNull,
};
const char* SettingTypeToString(SettingType);

struct SettingValue {
  SettingValue();   // Creates a kNull type.
  explicit SettingValue(bool);
  explicit SettingValue(int);
  explicit SettingValue(const char*);
  explicit SettingValue(std::string);
  explicit SettingValue(std::vector<std::string>);

  bool is_bool() const { return type == SettingType::kBoolean; }
  bool is_int() const { return type == SettingType::kInteger; }
  bool is_string() const { return type == SettingType::kString; }
  bool is_list() const { return type == SettingType::kList; }
  bool is_null() const { return type == SettingType::kNull; }

  const auto& get_bool() const { return std::get<bool>(value); }
  const auto& get_int() const { return std::get<int>(value); }
  const auto& get_string() const { return std::get<std::string>(value); }
  const auto& get_list() const {
    return std::get<std::vector<std::string>>(value);
  }

  void set_bool(bool v) { value = v; }
  void set_int(int v) { value = v; }
  void set_string(std::string v) { value = std::move(v); }
  void set_list(std::vector<std::string> v) { value = std::move(v); }


  using VariantValue =
      std::variant<bool, int, std::string, std::vector<std::string>>;

  SettingType type = SettingType::kNull;
  VariantValue value;
};

}  // namespace zxdb
