// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/zxdb/client/symbols/type.h"

namespace zxdb {

// Represents an array. An array is similar to a pointer but we specifically
// know its an array and know its length.
//
// DWARF says an array *may* have a length, but in practice Clang and GCC
// both define int[] as a pointer. Therefore, we require arrays to have
// known lengths.
class ArrayType final : public Type {
 public:
  const ArrayType* AsArrayType() const override;

  const LazySymbol& value_type() const { return value_type_; }
  size_t num_elts() const { return num_elts_; }

 private:
  FRIEND_REF_COUNTED_THREAD_SAFE(ArrayType);
  FRIEND_MAKE_REF_COUNTED(ArrayType);

  ArrayType(LazySymbol value_type, size_t num_elts);
  ~ArrayType() override;

  std::string ComputeFullName() const override;

  const LazySymbol value_type_;
  const size_t num_elts_;
};

}  // namespace zxdb
