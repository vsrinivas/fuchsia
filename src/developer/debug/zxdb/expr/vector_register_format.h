// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VECTOR_REGISTER_FORMAT_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VECTOR_REGISTER_FORMAT_H_

#include <stdint.h>

#include <optional>
#include <vector>

#include "src/developer/debug/shared/register_id.h"
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

// String constants for the above values.
extern const char kVectorRegisterFormatStr_Signed8[];
extern const char kVectorRegisterFormatStr_Unsigned8[];
extern const char kVectorRegisterFormatStr_Signed16[];
extern const char kVectorRegisterFormatStr_Unsigned16[];
extern const char kVectorRegisterFormatStr_Signed32[];
extern const char kVectorRegisterFormatStr_Unsigned32[];
extern const char kVectorRegisterFormatStr_Signed64[];
extern const char kVectorRegisterFormatStr_Unsigned64[];
extern const char kVectorRegisterFormatStr_Signed128[];
extern const char kVectorRegisterFormatStr_Unsigned128[];
extern const char kVectorRegisterFormatStr_Float[];
extern const char kVectorRegisterFormatStr_Double[];

const char* VectorRegisterFormatToString(VectorRegisterFormat fmt);

// Converts back from VectorRegisterFormatToString. A nullopt return value indicates failure.
std::optional<VectorRegisterFormat> StringToVectorRegisterFormat(const std::string& str);

// Converts the given vector register data to an array of the given format.
ExprValue VectorRegisterToValue(debug::RegisterID id, VectorRegisterFormat fmt,
                                std::vector<uint8_t> data);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_VECTOR_REGISTER_FORMAT_H_
