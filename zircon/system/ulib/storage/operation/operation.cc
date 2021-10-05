// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <safemath/checked_math.h>
#include <storage/operation/operation.h>
#ifdef __Fuchsia__
#include <storage/operation/unbuffered_operation.h>
#endif

#include <ostream>

namespace storage {

const char* OperationTypeToString(OperationType type) {
  switch (type) {
    case OperationType::kRead:
      return "kRead";
    case OperationType::kWrite:
      return "kWrite";
    case OperationType::kTrim:
      return "kTrim";
    default:
      return "<unknown>";
  }
}

template <typename T>
uint64_t BlockCountImpl(cpp20::span<const T> operations) {
  safemath::CheckedNumeric<uint64_t> total_length = 0;
  for (const auto& operation : operations) {
    total_length += operation.op.length;
  }
  return total_length.ValueOrDie();
}

uint64_t BlockCount(cpp20::span<const BufferedOperation> operations) {
  return BlockCountImpl(operations);
}

#ifdef __Fuchsia__
uint64_t BlockCount(cpp20::span<const UnbufferedOperation> operations) {
  return BlockCountImpl(operations);
}
#endif

std::ostream& operator<<(std::ostream& stream, const BufferedOperation& operation) {
  stream << "BufferedOperation {type: " << OperationTypeToString(operation.op.type)
         << " vmo_offset: " << operation.op.vmo_offset << " dev_offset: " << operation.op.dev_offset
         << " length: " << operation.op.length << "}";
  return stream;
}

std::ostream& operator<<(std::ostream& stream,
                         const cpp20::span<const BufferedOperation>& operations) {
  stream << "[";
  for (size_t i = 0; i < operations.size(); ++i) {
    if (i < operations.size() - 1) {
      stream << ", ";
    }
    stream << operations[i];
  }
  stream << "]";
  return stream;
}

}  // namespace storage
