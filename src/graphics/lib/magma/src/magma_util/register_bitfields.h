// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef REGISTER_BITFIELDS_H
#define REGISTER_BITFIELDS_H

#include <stdint.h>

#include <type_traits>

#include "magma_util/macros.h"
#include "register_io.h"

// This file provides some helpers for accessing bitfields in registers.
//
// Example usage:
//
//   // Define bitfields for an "AuxControl" register.
//   class AuxControl : public RegisterBase {
//   public:
//       // Define a single-bit field.
//       DEF_BIT(31, enabled);
//       // Define a 5-bit field, from bits 20-24 (inclusive).
//       DEF_FIELD(24, 20, message_size);
//
//       // Returns an object representing the register's type and address.
//       static auto Get() { return RegisterAddr<AuxControl>(0x64010); }
//   };
//
//   void Example1(RegisterIo* reg_io)
//   {
//       // Read the register's value from MMIO.  "reg" is a snapshot of the
//       // register's value which also knows the register's address.
//       auto reg = AuxControl::Get().ReadFrom(reg_io);
//
//       // Read this register's "message_size" field.
//       uint32_t size = reg.message_size().get();
//
//       // Change this field's value.  This modifies the snapshot.
//       reg.message_size().set(1234);
//
//       // Write the modified register value to MMIO.
//       reg.WriteTo(reg_io);
//   }
//
//   // It is also possible to write a register without having to read it
//   // first:
//   void Example2(RegisterIo* reg_io)
//   {
//       // Start off with a value that is initialized to zero.
//       auto reg = AuxControl::Get().FromValue(0);
//       // Fill out fields.
//       reg.message_size().set(2345);
//       // Write the register value to MMIO.
//       reg.WriteTo(reg_io);
//   }
//
// Note that this produces efficient code with GCC and Clang, which are
// capable of optimizing away the intermediate objects.
//
// The arguments to DEF_FIELD() are organized to match up with the
// documentation.  For example, if the docs specify a field as:
//   23:0  Data M value
// then that translates to:
//   DEF_FIELD(23, 0, data_m_value)
// To match up, we put the upper bit first and use an inclusive bit range.

namespace magma {

// An instance of RegisterBase represents a staging copy of a register,
// which can be written to the register itself.  It knows the register's
// address and stores a value for the register.
//
// Normal usage is to create classes that derive from RegisterBase and
// provide methods for accessing bitfields of the register.  RegisterBase
// does not provide a constructor because constructors are not inherited by
// derived classes by default, and we don't want the derived classes to
// have to declare constructors.
class RegisterBase {
 public:
  using ValueType = uint32_t;
  uint32_t reg_addr() { return reg_addr_; }
  void set_reg_addr(uint32_t addr) { reg_addr_ = addr; }

  uint32_t reg_value() { return reg_value_; }
  uint32_t* reg_value_ptr() { return &reg_value_; }
  void set_reg_value(uint32_t value) { reg_value_ = value; }

  void ReadFrom(RegisterIo* reg_io) { reg_value_ = reg_io->Read32(reg_addr_); }
  void WriteTo(RegisterIo* reg_io) { reg_io->Write32(reg_addr_, reg_value_); }

  void ReadFrom(magma::PlatformMmio* reg_io) { reg_value_ = reg_io->Read32(reg_addr_); }
  void WriteTo(magma::PlatformMmio* reg_io) { reg_io->Write32(reg_addr_, reg_value_); }

 private:
  uint32_t reg_addr_ = 0;
  uint32_t reg_value_ = 0;
};

// This is similar to a RegisterBase, but represents two registers which
// together hold a 64-bit value. The first contains the low 32 bits, and the
// second (offset 4) contains the high 32 bits.
class RegisterPairBase {
 public:
  using ValueType = uint64_t;
  uint32_t reg_addr() { return reg_addr_; }
  void set_reg_addr(uint32_t addr) { reg_addr_ = addr; }

  uint64_t reg_value() { return reg_value_; }
  uint64_t* reg_value_ptr() { return &reg_value_; }
  void set_reg_value(uint64_t value) { reg_value_ = value; }

  void ReadFrom(RegisterIo* reg_io) {
    uint64_t value_high = reg_io->Read32(reg_addr_ + 4);
    uint64_t value_low = reg_io->Read32(reg_addr_);
    reg_value_ = (value_high << 32) | value_low;
  }
  void WriteTo(RegisterIo* reg_io) {
    reg_io->Write32(reg_addr_, reg_value_ & 0xffffffff);
    reg_io->Write32(reg_addr_ + 4, reg_value_ >> 32);
  }

  void ReadFrom(magma::PlatformMmio* reg_io) {
    uint64_t value_high = reg_io->Read32(reg_addr_ + 4);
    uint64_t value_low = reg_io->Read32(reg_addr_);
    reg_value_ = (value_high << 32) | value_low;
  }
  void WriteTo(magma::PlatformMmio* reg_io) {
    reg_io->Write32(reg_addr_, reg_value_ & 0xffffffff);
    reg_io->Write32(reg_addr_ + 4, reg_value_ >> 32);
  }

 private:
  // Points to the low half of the register.
  uint32_t reg_addr_ = 0;
  uint64_t reg_value_ = 0;
};

// An instance of RegisterAddr represents a typed register address: It
// knows the address of the register (within the MMIO address space) and
// the type of its contents, RegType.  RegType represents the register's
// bitfields.  RegType should be a subclass of RegisterBase.
template <class RegType>
class RegisterAddr {
 public:
  RegisterAddr(uint32_t reg_addr) : reg_addr_(reg_addr) {}

  static_assert(std::is_base_of<RegisterBase, RegType>::value ||
                    std::is_base_of<RegisterPairBase, RegType>::value,
                "Parameter of RegisterAddr<> should derive from RegisterBase");

  // Instantiate a RegisterBase using the value of the register read from
  // MMIO.
  RegType ReadFrom(RegisterIo* reg_io) {
    RegType reg;
    reg.set_reg_addr(reg_addr_);
    reg.ReadFrom(reg_io);
    return reg;
  }

  RegType ReadFrom(magma::PlatformMmio* reg_io) {
    RegType reg;
    reg.set_reg_addr(reg_addr_);
    reg.ReadFrom(reg_io);
    return reg;
  }

  // Instantiate a RegisterBase using the given value for the register.
  RegType FromValue(typename RegType::ValueType value) {
    RegType reg;
    reg.set_reg_addr(reg_addr_);
    reg.set_reg_value(value);
    return reg;
  }

  uint32_t addr() { return reg_addr_; }

 private:
  uint32_t reg_addr_;
};

template <class IntType>
class BitfieldRef {
 public:
  BitfieldRef(IntType* value_ptr, uint32_t bit_high_incl, uint32_t bit_low)
      : value_ptr_(value_ptr),
        shift_(bit_low),
        mask_(magma::to_uint32((1ul << (bit_high_incl - bit_low + 1)) - 1)) {}

  uint32_t get() const { return (*value_ptr_ >> shift_) & mask_; }

  void set(uint32_t field_val) {
    DASSERT((field_val & ~mask_) == 0);
    *value_ptr_ &= ~(mask_ << shift_);
    *value_ptr_ |= (field_val << shift_);
  }

  // Allow both implicit conversion and get() to simplify the conversion of code to the hwreg
  // style.
  operator uint32_t() const { return get(); }

 private:
  IntType* value_ptr_;
  uint32_t shift_;
  uint32_t mask_;
};

#define DEF_FIELD(BIT_HIGH, BIT_LOW, NAME)                                          \
  static_assert((BIT_HIGH) > (BIT_LOW), "Upper bit goes before lower bit");         \
  static_assert((BIT_HIGH) < 32, "Upper bit is out of range");                      \
  magma::BitfieldRef<uint32_t> NAME() {                                             \
    return magma::BitfieldRef<uint32_t>(reg_value_ptr(), (BIT_HIGH), (BIT_LOW));    \
  }                                                                                 \
  auto& set_##NAME(uint32_t val) {                                                  \
    magma::BitfieldRef<ValueType>(reg_value_ptr(), (BIT_HIGH), (BIT_LOW)).set(val); \
    return *this;                                                                   \
  }

#define DEF_BIT(BIT, NAME)                                                 \
  static_assert((BIT) < 32, "Bit is out of range");                        \
  magma::BitfieldRef<uint32_t> NAME() {                                    \
    return magma::BitfieldRef<uint32_t>(reg_value_ptr(), (BIT), (BIT));    \
  }                                                                        \
  auto& set_##NAME(uint32_t val) {                                         \
    magma::BitfieldRef<ValueType>(reg_value_ptr(), (BIT), (BIT)).set(val); \
    return *this;                                                          \
  }

// This defines an accessor (named SUBFIELD_NAME) for a bit range of a
// field (named COMBINED_FIELD) in a struct.
#define DEF_SUBFIELD(COMBINED_FIELD, BIT_HIGH, BIT_LOW, SUBFIELD_NAME)          \
  magma::BitfieldRef<uint8_t> SUBFIELD_NAME() {                                 \
    return magma::BitfieldRef<uint8_t>(&COMBINED_FIELD, (BIT_HIGH), (BIT_LOW)); \
  }

}  // namespace magma

#endif
