// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BITFIELD_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BITFIELD_H_

#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/expr/eval_context.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class ExprValue;
class FoundMember;

// Extracts a member from a collection that's a bitfield.
ErrOrValue ResolveBitfieldMember(const fxl::RefPtr<EvalContext>& context, const ExprValue& base,
                                 const FoundMember& found_member);

// Writes the data to a "source" specification that's a bitfield. The data should contain the
// little-endian representation of the numeric value of the bitfield.
void WriteBitfieldToMemory(const fxl::RefPtr<EvalContext>& context, const ExprValueSource& dest,
                           std::vector<uint8_t> data, fit::callback<void(const Err&)> cb);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_BITFIELD_H_
