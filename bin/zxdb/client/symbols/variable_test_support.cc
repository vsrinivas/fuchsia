// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/symbols/variable_test_support.h"

#include "garnet/bin/zxdb/client/symbols/base_type.h"

namespace zxdb {

fxl::RefPtr<Variable> MakeUint64VariableForTest(
    const std::string& name, uint64_t begin_ip_range, uint64_t end_ip_range,
    std::vector<uint8_t> location_expression) {
  auto variable = fxl::MakeRefCounted<Variable>(Symbol::kTagVariable);
  variable->set_assigned_name(name);

  VariableLocation::Entry entry;
  entry.begin = begin_ip_range;
  entry.end = end_ip_range;
  entry.expression = std::move(location_expression);
  variable->set_location(VariableLocation({entry}));

  variable->set_type(LazySymbol(fxl::MakeRefCounted<BaseType>(
      BaseType::kBaseTypeUnsigned, 8, "uint64_t")));

  return variable;
}

}  // namespace zxdb
