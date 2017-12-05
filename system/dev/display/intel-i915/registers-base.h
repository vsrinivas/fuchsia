// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

#include "mmio-space.h"

namespace registers {

template <class RegType> class RegisterBase {
public:
    uint32_t reg_value() const { return reg_value_; }
    uint32_t* reg_value_ptr() { return &reg_value_; }
    void set_reg_addr(uint32_t addr) { reg_addr_ = addr; }
    void set_reg_value(uint32_t value) { reg_value_ = value; }

    RegType* ReadFrom(i915::MmioSpace* mmio_space) {
        reg_value_ = mmio_space->Read32(reg_addr_);
        return static_cast<RegType*>(this);
    }

    RegType* WriteTo(i915::MmioSpace* mmio_space) {
        mmio_space->Write32(reg_addr_, reg_value_);
        return static_cast<RegType*>(this);
    }

private:
    uint32_t reg_addr_ = 0;
    uint32_t reg_value_ = 0;
};

template <class RegType> class RegisterAddr {
public:
    RegisterAddr(uint32_t reg_addr) : reg_addr_(reg_addr) {}

    RegType ReadFrom(i915::MmioSpace* mmio_space)
    {
        RegType reg;
        reg.set_reg_addr(reg_addr_);
        reg.ReadFrom(mmio_space);
        return reg;
    }

    RegType FromValue(uint32_t value)
    {
        RegType reg;
        reg.set_reg_addr(reg_addr_);
        reg.set_reg_value(value);
        return reg;
    }

    uint32_t addr() { return reg_addr_; }

private:
    uint32_t reg_addr_;
};

template <class IntType> class BitfieldRef {
public:
    BitfieldRef(IntType* value_ptr, uint32_t bit_high_incl, uint32_t bit_low)
        : value_ptr_(value_ptr), shift_(bit_low), mask_((1 << (bit_high_incl - bit_low + 1)) - 1)
    {
        static_assert(sizeof(IntType) <= sizeof(uint32_t), "BitfieldRef too large");
    }

    uint32_t get() { return (*value_ptr_ >> shift_) & mask_; }

    void set(IntType field_val)
    {
        assert((field_val & ~mask_) == 0);
        *value_ptr_ &= ~(mask_ << shift_);
        *value_ptr_ |= (field_val << shift_);
    }

private:
    IntType* value_ptr_;
    uint32_t shift_;
    uint32_t mask_;
};

#define DEF_FIELD(BIT_HIGH, BIT_LOW, NAME)                                                         \
    static_assert((BIT_HIGH) > (BIT_LOW), "Upper bit goes before lower bit");                      \
    static_assert((BIT_HIGH) < 32, "Upper bit is out of range");                                   \
    registers::BitfieldRef<uint32_t> NAME() {                                                      \
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), (BIT_HIGH), (BIT_LOW));           \
    }

#define DEF_BIT(BIT, NAME)                                                                         \
    static_assert((BIT) < 32, "Bit is out of range");                                              \
    registers::BitfieldRef<uint32_t> NAME() {                                                      \
        return registers::BitfieldRef<uint32_t>(reg_value_ptr(), (BIT), (BIT));                    \
    }

#define DEF_SUBFIELD(COMBINED_FIELD, BIT_HIGH, BIT_LOW, SUBFIELD_NAME)                             \
    static_assert((BIT_HIGH) >= (BIT_LOW), "Upper bit goes before lower bit");                     \
    static_assert((BIT_HIGH) < 8, "Upper bit is out of range");                                    \
    registers::BitfieldRef<uint8_t> SUBFIELD_NAME() {                                              \
        return registers::BitfieldRef<uint8_t>(&COMBINED_FIELD, (BIT_HIGH), (BIT_LOW));            \
    }

} // namespace registers
