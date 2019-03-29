// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>

#include <vector>

#include "garnet/bin/zxdb/symbols/type.h"
#include "src/developer/debug/zxdb/expr/expr_value_source.h"
#include "src/lib/fxl/memory/ref_ptr.h"

namespace zxdb {

class Err;
class Type;

// Holds a value for an expression. This could be the value of a variable in
// memory (e.g. the value of "a" when you type "print a"), or it could be
// a temporary that the debugger has computed as part of an expression.
class ExprValue {
 public:
  ExprValue();

  // Constructs a value from the corresponding C++ value. A Type will be
  // created to represent the value.
  explicit ExprValue(bool value);
  explicit ExprValue(int8_t value);
  explicit ExprValue(uint8_t value);
  explicit ExprValue(int16_t value);
  explicit ExprValue(uint16_t value);
  explicit ExprValue(int32_t value);
  explicit ExprValue(uint32_t value);
  explicit ExprValue(int64_t value);
  explicit ExprValue(uint64_t value);

  // Full constructor.
  ExprValue(fxl::RefPtr<Type> symbol_type, std::vector<uint8_t> data,
            const ExprValueSource& source = ExprValueSource());
  ~ExprValue();

  // Used for tests. If a SymbolType is defined, the string representation is
  // compared since the pointers may not match in practice.
  bool operator==(const ExprValue& other) const;

  // May be null if there's no symbol type.
  Type* type() const { return type_.get(); }
  const fxl::RefPtr<Type>& type_ref() const { return type_; }

  // Indicates the location where this value came from.
  const ExprValueSource& source() const { return source_; }

  const std::vector<uint8_t>& data() const { return data_; }

  // Determines which base type the Value's Type is.
  //
  // TODO(brettw) the base type should probably be turned into a proper enum.
  int GetBaseType() const;

  // Returns an error if the size of the data doesn't match the parameter.
  Err EnsureSizeIs(size_t size) const;

  // These return the data casted to the corresponding value (specializations
  // below class declaration). It will assert if the internal type and data
  // size doesn't match the requested type.
  template <typename T>
  T GetAs() const;

  // Gets the result as a [u]int64_t, promoting all shorter values to the
  // longer ones. If the data size is empty or greater than 64 bits it will
  // return an error.
  Err PromoteTo64(int64_t* output) const;
  Err PromoteTo64(uint64_t* output) const;

  // Gets the result as a double. This will convert floats and doubles to
  // doubles.
  Err PromoteToDouble(double* output) const;

 private:
  // Internal constructor for the primitive types that constructs an on-the-fly
  // type definition for the built-in type.
  ExprValue(int base_type, const char* type_name, void* data,
            uint32_t data_size);

  // Application-defined type from the symbols.
  fxl::RefPtr<Type> type_;

  ExprValueSource source_;

  std::vector<uint8_t> data_;
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
float ExprValue::GetAs<float>() const;

template <>
double ExprValue::GetAs<double>() const;

}  // namespace zxdb
