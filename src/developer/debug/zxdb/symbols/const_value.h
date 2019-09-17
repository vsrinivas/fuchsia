// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CONST_VALUE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CONST_VALUE_H_

#include <stdint.h>

#include <vector>

namespace zxdb {

// Representation used for Values (DataMembers and Variables) that have a constant value. These
// members have no in-memory storage.
//
// The encoding for const values is complicated which necessitates this class. The storage
// represents "whatever the value looks like in memory".
//
// In practice, our Clang outputs:
//   - DW_FORM_sdata / DW_FORM_udata for normal signed and unsigned numbers. In this case we don't
//     know the size of the output without doing lots more work.
//   - DW_FORM_block* for other stuff. We assume the size is correct in this case.
//
// We choose not to be very smart about how to store integer data. Theoretically, the types might
// not be completely known when decoding a variable. There might be forward-defined types that need
// the symbol index, and resolving things like typedefs can be complicated. We choose to punt that
// to a higher layer and only store the data here with minimal semantic knowledge. Numbers come
// out of LLVM's decoder as 64-bit integers so this is what we handle. Anything larger than this
// must be expressed as a "block" and have the correct size.
class ConstValue {
 public:
  // Initializes has having no const value.
  ConstValue() = default;

  // Use for numbers. For unsigned numbers, cast to a signed value. The bytes will be copied out of
  // this value from the low byte and as long as it's sign extended when necessary the results will
  // be correct for unsigned and signed numbers.
  explicit ConstValue(int64_t v);

  // Use for arbitrary data.
  explicit ConstValue(std::vector<uint8_t> buffer);

  // Returns whether this holds a value. When this returns false, the associated value (normally the
  // one that "this" is a member of) has no "const value" and it refers to a real variable in
  // memory.
  bool has_value() const { return !data_.empty(); }

  // Copies the const value out to a memory buffer of the requested size.
  //
  // This will assert if !has_value().
  //
  // If the requested size is smaller than the data we have, the data will be truncated. Since we
  // assume little-endian, this will do the right thing for numbers < 64 bits. If the requested
  // size is larger, it will be 0-filled on the right. This behavior is because const values are
  // normally used just for integers and its difficult to know the correct size when the attribute
  // is being decoded.
  //
  // (If we need to support big endian we probably want to mark the "number" case in the constructor
  // so we know how to truncate in this function).
  std::vector<uint8_t> GetConstValue(size_t byte_count) const;

 private:
  std::vector<uint8_t> data_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_SYMBOLS_CONST_VALUE_H_
