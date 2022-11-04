// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_VALUE_SOURCE_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_VALUE_SOURCE_H_

#include <stdint.h>

#include "src/developer/debug/shared/register_id.h"
#include "src/developer/debug/zxdb/common/int128_t.h"
#include "src/lib/fxl/memory/ref_counted.h"

namespace zxdb {

class LocalExprValue;

// Holds the source of a value. This allows taking the address of an object stored in an ExprValue
// ("&foo"), and for updating the contents of variables (currently not supported yet).
class ExprValueSource {
 public:
  // Where this value came from.
  enum class Type {
    // No source, this is the result of some computation.
    kTemporary,

    // The value lived in memory at the specified address.
    kMemory,

    // The value came from the specified CPU register.
    kRegister,

    // The value is known to be constant and can not be changed. The difference between this and
    // "temporary" is really just messaging since neither can be modified.
    kConstant,

    // This value came from more than one place. The optimizer can sometimes split things up,
    // for example, a pair might be put into two CPU registers, one for each value. There can also
    // be composite CPU/memory ones if something is in memory, but a modification to that is
    // only stored in a register.
    //
    // We currently don't support this and this enum indicates that the value can't be modified.
    // But we can message that it could be with additional feature work.
    //
    // TODO(bug 39630) the ExprValueSource should probably have a vector of sub-regions, each with
    // their own ExprValueSource. When we extract structure members, also extract the correct
    // sub-region(s).
    kComposite,

    // This value is local to the debugger frontend. It can be read and set, but its value does
    // not reflect or change anything in the target program.
    kLocal,
  };

  // Returns a string corresponding to the given type, "register", "temporary", etc.
  static const char* TypeToString(Type t);

  // Indicates an unknown, temporary (the output of "i + 4"), or constant source.
  explicit ExprValueSource(Type type = Type::kTemporary);

  // Initializes indicating a memory address and optional bitfield information.
  explicit ExprValueSource(uint64_t address, uint32_t bit_size = 0, uint32_t bit_shift = 0);

  // Initializes indicating a register and optional bitfield information. The register does not have
  // to be a canonical register.
  explicit ExprValueSource(debug::RegisterID id, uint32_t bit_size = 0, uint32_t bit_shift = 0);

  // Initializes indicating a reference to a local value.
  explicit ExprValueSource(fxl::RefPtr<LocalExprValue> local_source);

  ExprValueSource(const ExprValueSource& other);
  ExprValueSource(ExprValueSource&& other);

  ~ExprValueSource();

  ExprValueSource& operator=(const ExprValueSource& other);
  ExprValueSource& operator=(ExprValueSource&& other);

  Type type() const { return type_; }

  bool is_bitfield() const { return bit_size_ != 0; }

  // Valid when type_ == kAddress.
  uint64_t address() const { return address_; }

  // Valid when type_ == kRegister.
  debug::RegisterID register_id() const { return register_id_; }

  // Number of bits used for bitfields. 0 means it is not a bitfield and all bits are used.
  uint32_t bit_size() const { return bit_size_; }

  // Number of bits to shift to the left to get the storage location. This is the offset of the low
  // bit. Note that this is different than the DWARF definition.
  //
  // If a bitfield occupies bits 3-6 (inclusive) of a 32-bit integer:
  //
  //   high                            low
  //    3           2         1          0
  //   10987654 32109876 54321098 76543210
  //                               [--]
  //                                   <--  bit_shift
  //
  // Then the bit_size() will be 4 and the bit_shift() will be 3.
  //
  // The memory layout will be the result of doing the shift and mask and memcpy-ing out which
  // will reorder the bytes in little-endian.
  uint32_t bit_shift() const { return bit_shift_; }

  // Value when type_ == kLocal.
  const fxl::RefPtr<LocalExprValue>& local_value() const { return local_value_; }

  // Returns a new ExprValueSource pointing to the given offset inside of this one. If this one is
  // not in memory, the returned one will be the same.
  //
  // When computing offsets of bitfields, the shifts are just added to any existing one, but the bit
  // size (if given) will overwrite any existing one.
  ExprValueSource GetOffsetInto(uint32_t offset, uint32_t new_bit_size = 0,
                                uint32_t bit_shift = 0) const {
    if (type_ == Type::kMemory) {
      return ExprValueSource(address_ + offset, new_bit_size == 0 ? bit_size_ : new_bit_size,
                             bit_shift_ + bit_shift);
    }
    return ExprValueSource();
  }

  // Writes the |new_value| over some |existing| value, taking into account the bit size and
  // shift information from this ExprValueSource. The returned value can be used to update the
  // register or memory for a bitfield.
  //
  // This ExprValueSource must be a bitfield (is_bitfield() == true) for this to be called.
  uint128_t SetBits(uint128_t existing, uint128_t new_value) const;

  bool operator==(const ExprValueSource& other) const {
    return type_ == other.type_ && address_ == other.address_ && bit_size_ == other.bit_size_ &&
           bit_shift_ == other.bit_shift_;
  }
  bool operator!=(const ExprValueSource& other) const { return !operator==(other); }

 private:
  Type type_ = Type::kTemporary;
  uint64_t address_ = 0;
  debug::RegisterID register_id_ = debug::RegisterID::kUnknown;

  uint32_t bit_size_ = 0;
  uint32_t bit_shift_ = 0;

  // Indicates the associated local value, set when type_ == kLocal.
  fxl::RefPtr<LocalExprValue> local_value_;
};

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_EXPR_VALUE_SOURCE_H_
