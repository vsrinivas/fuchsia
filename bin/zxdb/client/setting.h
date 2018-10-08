// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "garnet/bin/zxdb/common/err.h"

namespace zxdb {

// Settings hold a variant value an interface to query/modify them.
class Setting {
 public:
  enum class Type : uint32_t {
    kBoolean = 0,
    kInteger,
    kString,
    kStringList,
    kNull,
  };

  // Will have type none. Will assert on any getter.
  Setting();
  explicit Setting(bool);
  explicit Setting(int);
  explicit Setting(const char*);
  explicit Setting(std::string);
  explicit Setting(std::vector<std::string>);

  Type type() const { return type_; }
  bool is_bool() const { return type_ == Type::kBoolean; }
  bool is_int() const { return type_ == Type::kInteger; }
  bool is_string() const { return type_ == Type::kString; }
  bool is_string_list() const { return type_ == Type::kStringList; }
  bool is_null() const { return type_ == Type::kNull; }

  bool valid() const { return type_ != Type::kNull; }

  // IMPORTANT: getters will assert if the wrong typeis used.
  //            This will help catch bugs earlier.
  bool& GetBool();
  bool GetBool() const;

  int& GetInt();
  int GetInt() const;

  std::string& GetString();
  const std::string& GetString() const;

  std::vector<std::string>& GetStringList();
  const std::vector<std::string>& GetStringList() const;

 private:
  void Init();

  using VariantValue =
      std::variant<bool, int, std::string, std::vector<std::string>>;

  Type type_ = Type::kNull;
  VariantValue values_;
};

}  // namespace zxdb
