// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

fxl::RefPtr<Variable> MakeVariableForTest(const std::string& name, fxl::RefPtr<Type> type,
                                          VariableLocation loc) {
  auto variable = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable);
  variable->set_assigned_name(name);
  variable->set_location(std::move(loc));
  variable->set_type(std::move(type));

  return variable;
}

fxl::RefPtr<Variable> MakeVariableForTest(const std::string& name, fxl::RefPtr<Type> type,
                                          uint64_t begin_ip_range, uint64_t end_ip_range,
                                          DwarfExpr location_expression) {
  // If this triggers, your range is invalid. If you want an always-valid range, use the other
  // variant of the function that takes a VariableLocation() and supply a default one.
  FX_DCHECK(begin_ip_range < end_ip_range);

  VariableLocation::Entry entry;
  entry.range = AddressRange(begin_ip_range, end_ip_range);
  entry.expression = std::move(location_expression);

  return MakeVariableForTest(name, std::move(type), VariableLocation({entry}));
}

fxl::RefPtr<Variable> MakeUint64VariableForTest(const std::string& name, VariableLocation loc) {
  return MakeVariableForTest(
      name, fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t"),
      std::move(loc));
}

fxl::RefPtr<Variable> MakeUint64VariableForTest(const std::string& name, uint64_t begin_ip_range,
                                                uint64_t end_ip_range,
                                                DwarfExpr location_expression) {
  return MakeVariableForTest(
      name, fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t"),
      begin_ip_range, end_ip_range, std::move(location_expression));
}

}  // namespace zxdb
