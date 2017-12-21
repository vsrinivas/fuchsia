// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/internal.h>
#include <hwreg/mmio.h>

#include <fbl/type_support.h>
#include <limits.h>
#include <stdint.h>
#include <zircon/assert.h>

// This file provides some helpers for accessing bitfields in registers.
//
// Example usage:
//
//   // Define bitfields for an "AuxControl" 32-bit register.
//   class AuxControl : public hwreg::RegisterBase<uint32_t> {
//   public:
//       // Define a single-bit field.
//       DEF_BIT(31, enabled);
//       // Define a 5-bit field, from bits 20-24 (inclusive).
//       DEF_FIELD(24, 20, message_size);
//
//       // Bits [30:25] and [19:0] are automatically preserved across RMW cycles
//
//       // Returns an object representing the register's type and address.
//       static auto Get() { return hwreg::RegisterAddr<AuxControl>(0x64010); }
//   };
//
//   void Example1(hwreg::RegisterIo* reg_io) {
//       // Read the register's value from MMIO.  "reg" is a snapshot of the
//       // register's value which also knows the register's address.
//       auto reg = AuxControl::Get().ReadFrom(reg_io);
//
//       // Read this register's "message_size" field.
//       uint32_t size = reg.message_size();
//
//       // Change this field's value.  This modifies the snapshot.
//       reg.set_message_size(1234);
//
//       // Write the modified register value to MMIO.
//       reg.WriteTo(reg_io);
//   }
//
//   // It is also possible to write a register without having to read it
//   // first:
//   void Example2(hwreg::RegisterIo* reg_io) {
//       // Start off with a value that is initialized to zero.
//       auto reg = AuxControl::Get().FromValue(0);
//       // Fill out fields.
//       reg.set_message_size(2345);
//       // Write the register value to MMIO.
//       reg.WriteTo(reg_io);
//   }
//
// Note that this produces efficient code with GCC and Clang, which are
// capable of optimizing away the intermediate objects.
//
// The arguments to DEF_FIELD() are organized to match up with Intel's
// documentation for their graphics hardware.  For example, if the docs
// specify a field as:
//   23:0  Data M value
// then that translates to:
//   DEF_FIELD(23, 0, data_m_value)
// To match up, we put the upper bit first and use an inclusive bit range.

namespace hwreg {

// An instance of RegisterBase represents a staging copy of a register,
// which can be written to the register itself.  It knows the register's
// address and stores a value for the register.
//
// Normal usage is to create classes that derive from RegisterBase and
// provide methods for accessing bitfields of the register.  RegisterBase
// does not provide a constructor because constructors are not inherited by
// derived classes by default, and we don't want the derived classes to
// have to declare constructors.
//
// Any bits not declared using DEF_FIELD/DEF_BIT/DEF_RSVDZ_FIELD/DEF_RSVDZ_BIT
// will be automatically preserved across RMW operations.
template <class IntType> class RegisterBase {
    static_assert(internal::IsSupportedInt<IntType>::value, "unsupported register access width");
public:
    using ValueType = IntType;
    uint32_t reg_addr() const { return reg_addr_; }
    void set_reg_addr(uint32_t addr) { reg_addr_ = addr; }

    ValueType reg_value() const { return reg_value_; }
    ValueType* reg_value_ptr() { return &reg_value_; }
    const ValueType* reg_value_ptr() const { return &reg_value_; }
    void set_reg_value(IntType value) { reg_value_ = value; }

    void ReadFrom(RegisterIo* reg_io) {
        reg_value_ = reg_io->Read<ValueType>(reg_addr_);
    }
    void WriteTo(RegisterIo* reg_io) {
        reg_io->Write(reg_addr_, static_cast<IntType>(reg_value_ & ~rsvdz_mask_));
    }

private:
    friend internal::RsvdZField<ValueType>;
    ValueType rsvdz_mask_ = 0;

    uint32_t reg_addr_ = 0;
    ValueType reg_value_ = 0;
};

// An instance of RegisterAddr represents a typed register address: It
// knows the address of the register (within the MMIO address space) and
// the type of its contents, RegType.  RegType represents the register's
// bitfields.  RegType should be a subclass of RegisterBase.
template <class RegType> class RegisterAddr {
public:
    RegisterAddr(uint32_t reg_addr) : reg_addr_(reg_addr) {}

    static_assert(fbl::is_base_of<RegisterBase<typename RegType::ValueType>, RegType>::value,
                  "Parameter of RegisterAddr<> should derive from RegisterBase");

    // Instantiate a RegisterBase using the value of the register read from
    // MMIO.
    RegType ReadFrom(RegisterIo* reg_io) {
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

    uint32_t addr() const { return reg_addr_; }

private:
    const uint32_t reg_addr_;
};

template <class IntType> class BitfieldRef {
public:
    BitfieldRef(IntType* value_ptr, uint32_t bit_high_incl, uint32_t bit_low)
        : value_ptr_(value_ptr), shift_(bit_low),
          mask_(internal::ComputeMask<IntType>(bit_high_incl - bit_low + 1)) {
    }

    IntType get() const { return static_cast<IntType>((*value_ptr_ >> shift_) & mask_); }

    void set(IntType field_val) {
        static_assert(!fbl::is_const<IntType>::value, "");
        ZX_DEBUG_ASSERT((field_val & ~mask_) == 0);
        *value_ptr_ = static_cast<IntType>(*value_ptr_ & ~(mask_ << shift_));
        *value_ptr_ = static_cast<IntType>(*value_ptr_ | (field_val << shift_));
    }

private:
    IntType* const value_ptr_;
    const uint32_t shift_;
    const IntType mask_;
};

// Declares multi-bit fields in a derived class of RegisterBase<T>.  This
// produces functions "T NAME() const" and "void set_NAME(T)".  Both bit indices
// are inclusive.
#define DEF_FIELD(BIT_HIGH, BIT_LOW, NAME)                                                        \
    static_assert((BIT_HIGH) >= (BIT_LOW), "Upper bit goes before lower bit");                    \
    static_assert((BIT_HIGH) < sizeof(ValueType) * CHAR_BIT, "Upper bit is out of range");        \
    ValueType NAME() const {                                                                      \
        return hwreg::BitfieldRef<const ValueType>(reg_value_ptr(), (BIT_HIGH), (BIT_LOW)).get(); \
    }                                                                                             \
    void set_ ## NAME(ValueType val) {                                                            \
        hwreg::BitfieldRef<ValueType>(reg_value_ptr(), (BIT_HIGH), (BIT_LOW)).set(val);           \
    }

// Declares single-bit fields in a derived class of RegisterBase<T>.  This
// produces functions "T NAME() const" and "void set_NAME(T)".
#define DEF_BIT(BIT, NAME) DEF_FIELD(BIT, BIT, NAME)

// Declares multi-bit reserved-zero fields in a derived class of RegisterBase<T>.
// This will ensure that on RegisterBase<T>::WriteTo(), reserved-zero bits are
// automatically zeroed.  Both bit indices are inclusive.
#define DEF_RSVDZ_FIELD(BIT_HIGH, BIT_LOW)                                                        \
    static_assert((BIT_HIGH) >= (BIT_LOW), "Upper bit goes before lower bit");                    \
    static_assert((BIT_HIGH) < sizeof(ValueType) * CHAR_BIT, "Upper bit is out of range");        \
    hwreg::internal::RsvdZField<ValueType> RsvdZ ## BIT_HIGH ## _ ## BIT_LOW =                    \
        hwreg::internal::RsvdZField<ValueType>(this, (BIT_HIGH), (BIT_LOW));

// Declares single-bit reserved-zero fields in a derived class of RegisterBase<T>.
// This will ensure that on RegisterBase<T>::WriteTo(), reserved-zero bits are
// automatically zeroed.
#define DEF_RSVDZ_BIT(BIT) DEF_RSVDZ_FIELD(BIT, BIT)

// Declares "decltype(FIELD) NAME() const" and "void set_NAME(decltype(FIELD))" that
// reads/modifies the declared bitrange.  Both bit indices are inclusive.
#define DEF_SUBFIELD(FIELD, BIT_HIGH, BIT_LOW, NAME)                                              \
    static_assert(hwreg::internal::IsSupportedInt<                                                \
                      typename fbl::remove_reference<decltype(FIELD)>::type>::value,              \
                  #FIELD " has unsupported type");                                                \
    static_assert((BIT_HIGH) >= (BIT_LOW), "Upper bit goes before lower bit");                    \
    static_assert((BIT_HIGH) < sizeof(decltype(FIELD)) * CHAR_BIT, "Upper bit is out of range");  \
    typename fbl::remove_reference<decltype(FIELD)>::type NAME() const {                          \
        return hwreg::BitfieldRef<const typename fbl::remove_reference<decltype(FIELD)>::type>(   \
            &FIELD, (BIT_HIGH), (BIT_LOW)).get();                                                 \
    }                                                                                             \
    void set_ ## NAME(typename fbl::remove_reference<decltype(FIELD)>::type val) {                \
        hwreg::BitfieldRef<typename fbl::remove_reference<decltype(FIELD)>::type>(                \
                &FIELD, (BIT_HIGH), (BIT_LOW)).set(val);                                          \
    }

// Declares "decltype(FIELD) NAME() const" and "void set_NAME(decltype(FIELD))" that
// reads/modifies the declared bit.
#define DEF_SUBBIT(FIELD, BIT, NAME) DEF_SUBFIELD(FIELD, BIT, BIT, NAME)

} // namespace hwreg
