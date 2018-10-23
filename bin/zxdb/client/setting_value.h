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

// Helper for clearer errors.
const char* SettingTypeToString(SettingType);

// SettingValues hold a variant value an interface to query/modify them.
class SettingValue {
 public:
  // Will have type none. Will assert on any getter.
  SettingValue();
  explicit SettingValue(bool);
  explicit SettingValue(int);
  explicit SettingValue(const char*);
  explicit SettingValue(std::string);
  explicit SettingValue(std::vector<std::string>);

  SettingType type() const { return type_; }
  bool is_bool() const { return type_ == SettingType::kBoolean; }
  bool is_int() const { return type_ == SettingType::kInteger; }
  bool is_string() const { return type_ == SettingType::kString; }
  bool is_list() const { return type_ == SettingType::kList; }
  bool is_null() const { return type_ == SettingType::kNull; }

  bool valid() const { return type_ != SettingType::kNull; }

  // IMPORTANT: getters will assert if the wrong type is used.
  //            This will help catch bugs earlier.
  bool& GetBool();
  bool GetBool() const;

  int& GetInt();
  int GetInt() const;

  std::string& GetString();
  const std::string& GetString() const;

  std::vector<std::string>& GetList();
  const std::vector<std::string>& GetList() const;

 private:
  void Init();

  using VariantValue =
      std::variant<bool, int, std::string, std::vector<std::string>>;

  SettingType type_ = SettingType::kNull;
  VariantValue value_;
};

}  // namespace zxdb
