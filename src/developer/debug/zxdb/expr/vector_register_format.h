// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VECTOR_REGISTER_FORMAT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VECTOR_REGISTER_FORMAT_H_

#include <stdint.h>

#include <vector>

#include "src/developer/debug/ipc/register_desc.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

enum class VectorRegisterFormat {
  kSigned8,
  kUnsigned8,
  kSigned16,
  kUnsigned16,
  kSigned32,
  kUnsigned32,
  kSigned64,
  kUnsigned64,
  kSigned128,
  kUnsigned128,
  kFloat,
  kDouble,
};

const char* VectorRegisterFormatToString(VectorRegisterFormat fmt);

// Converts the given vector register data to an array of the given format.
ExprValue VectorRegisterToValue(VectorRegisterFormat fmt, std::vector<uint8_t> data);

// Returns true if the given register should be formatted as a vector register.
//
// This is not quite the same as checking the category because some control registers are in the
// "vector" category, and x86 has the mmx registers in the FP category because they're aliased on
// the FP ones.
bool ShouldFormatRegisterAsVector(debug_ipc::RegisterID id);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VECTOR_REGISTER_FORMAT_H_
