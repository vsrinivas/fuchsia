// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_VALUE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_VALUE_H_

#include <inttypes.h>

#include <iosfwd>
#include <vector>

#include "lib/fit/function.h"
#include "src/developer/debug/zxdb/common/err_or.h"
#include "src/developer/debug/zxdb/common/int128_t.h"
#include "src/developer/debug/zxdb/common/tagged_data.h"
#include "src/developer/debug/zxdb/expr/expr_value_source.h"
#include "src/developer/debug/zxdb/symbols/type.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class EvalContext;
class Type;

// Holds a value for an expression. This could be the value of a variable in memory (e.g. the value
// of "a" when you type "print a"), or it could be a temporary that the debugger has computed as
// part of an expression.
class ExprValue {
 public:
  ExprValue();

  // Constructs a value from the corresponding C++ value of the given size.
  //
  // If the type is null, a type matching the corresponding C++ parameter name ("int32_t", etc.)
  // will be created to represent the value (this is useful for tests).
  explicit ExprValue(bool value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(int8_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(uint8_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(int16_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(uint16_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(int32_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(uint32_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(int64_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(uint64_t value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(float value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());
  explicit ExprValue(double value, fxl::RefPtr<Type> type = fxl::RefPtr<Type>(),
                     const ExprValueSource& source = ExprValueSource());

  // Full constructor. This takes the type and stores it assuming the type is good. Prefer the other
  // version when possible unless you're sure the type is not a declaration.
  ExprValue(fxl::RefPtr<Type> symbol_type, std::vector<uint8_t> data,
            const ExprValueSource& source = ExprValueSource());
  ExprValue(fxl::RefPtr<Type> symbol_type, TaggedData data,
            const ExprValueSource& source = ExprValueSource());

  ~ExprValue();

  // Used for tests. If a SymbolType is defined, the string representation is compared since the
  // pointers may not match in practice.
  bool operator==(const ExprValue& other) const;
  bool operator!=(const ExprValue& other) const { return !operator==(other); }

  // May be null if there's no symbol type.
  Type* type() const { return type_.get(); }
  const fxl::RefPtr<Type>& type_ref() const { return type_; }

  // Indicates the location where this value came from.
  const ExprValueSource& source() const { return source_; }

  const TaggedData& data() const { return data_; }

  // Determines which base type the Value's Type is.
  //
  // TODO(brettw) this should be removed, it does not support forward definitions. Callers should
  // interrogate GetConcreteType() instead.
  int GetBaseType() const;

  // Returns an "optimized out" error if not all bytes of the tagged buffer are marked valid.
  Err EnsureAllValid() const;

  // Returns an error if the size of the data doesn't match the parameter.
  Err EnsureSizeIs(size_t size) const;

  // These return the data casted to the corresponding value (specializations below class
  // declaration). It will assert if the internal type and data size doesn't match the requested
  // type.
  template <typename T>
  T GetAs() const;

  // Gets the result as the given 64/128-bit value, promoting all shorter values to the longer ones.
  // If the data size is empty or greater than the requested bits it will return an error.
  Err PromoteTo64(int64_t* output) const;
  Err PromoteTo64(uint64_t* output) const;
  Err PromoteTo128(int128_t* output) const;
  Err PromoteTo128(uint128_t* output) const;

  // Gets the result as a double. This will convert floats and doubles to doubles. It will not
  // convert ints to floating point.
  Err PromoteToDouble(double* output) const;

 private:
  // Internal constructor for the primitive types. It uses the given |type| if given, otherwise they
  // construct an on-the-fly type definition for the built-in type with the given parameters.
  ExprValue(fxl::RefPtr<Type> optional_type, int base_type, const char* type_name, void* data,
            uint32_t data_size, const ExprValueSource& source);

  // Application-defined type from the symbols.
  fxl::RefPtr<Type> type_;

  ExprValueSource source_;

  // The raw bytes of the value. This is a tagged data buffer to allow us to express that certain
  // bytes may be valid while others might be unknown. This can happen for optimized code where,
  // for example, some portions of a struct are kept in registers so can be known, but other
  // portions of the struct are optimized out.
  TaggedData data_;
};

template <>
int8_t ExprValue::GetAs<int8_t>() const;

template <>
uint8_t ExprValue::GetAs<uint8_t>() const;

template <>
int16_t ExprValue::GetAs<int16_t>() const;

template <>
uint16_t ExprValue::GetAs<uint16_t>() const;

template <>
int32_t ExprValue::GetAs<int32_t>() const;

template <>
uint32_t ExprValue::GetAs<uint32_t>() const;

template <>
int64_t ExprValue::GetAs<int64_t>() const;

template <>
uint64_t ExprValue::GetAs<uint64_t>() const;

template <>
int128_t ExprValue::GetAs<int128_t>() const;

template <>
uint128_t ExprValue::GetAs<uint128_t>() const;

template <>
float ExprValue::GetAs<float>() const;

template <>
double ExprValue::GetAs<double>() const;

// ExprValues are often returned or passed in an "ErrOr" structure to also track error state.
using ErrOrValue = ErrOr<ExprValue>;

using ErrOrValueVector = ErrOr<std::vector<ExprValue>>;

// Unit tests often use ExprValue and we want it to print in unit tests.
std::ostream& operator<<(std::ostream& out, const ExprValue& value);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_VALUE_H_
