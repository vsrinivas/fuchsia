// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string>

#include "garnet/bin/zxdb/client/symbols/symbol.h"

namespace zxdb {

// A value is the base class for data with names: parameters, variables, and
// struct/class data members.
class Value : public Symbol {
 public:
  // Don't construct by itself, used as a base class for Variable and
  // DataMember.

  // Symbol overrides.
  const Value* AsValue() const override;
  const std::string& GetAssignedName() const final { return assigned_name_; }

  // The name of the variable, parameter, or member name. See
  // Symbol::GetAssignedName().
  void set_assigned_name(const std::string& n) { assigned_name_ = n; }

  const LazySymbol& type() const { return type_; }
  void set_type(const LazySymbol& t) { type_ = t; }

  // This could add the decl_file/line if we need it since normally such
  // entries have this information.

 protected:
  explicit Value(int tag);
  ~Value();

 private:
  std::string assigned_name_;
  LazySymbol type_;
};

}  // namespace zxdb
