// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/symbols/variable_test_support.h"

#include "src/developer/debug/zxdb/symbols/base_type.h"

namespace zxdb {

fxl::RefPtr<Variable> MakeVariableForTest(
    const std::string& name, fxl::RefPtr<Type> type, uint64_t begin_ip_range,
    uint64_t end_ip_range, std::vector<uint8_t> location_expression) {
  auto variable = fxl::MakeRefCounted<Variable>(DwarfTag::kVariable);
  variable->set_assigned_name(name);

  VariableLocation::Entry entry;
  entry.begin = begin_ip_range;
  entry.end = end_ip_range;
  entry.expression = std::move(location_expression);
  variable->set_location(VariableLocation({entry}));
  variable->set_type(LazySymbol(std::move(type)));

  return variable;
}

fxl::RefPtr<Variable> MakeUint64VariableForTest(
    const std::string& name, uint64_t begin_ip_range, uint64_t end_ip_range,
    std::vector<uint8_t> location_expression) {
  return MakeVariableForTest(
      name,
      fxl::MakeRefCounted<BaseType>(BaseType::kBaseTypeUnsigned, 8, "uint64_t"),
      begin_ip_range, end_ip_range, std::move(location_expression));
}

}  // namespace zxdb
