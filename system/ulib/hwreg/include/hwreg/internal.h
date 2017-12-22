// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/algorithm.h>
#include <fbl/type_support.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <zircon/assert.h>

namespace hwreg {

struct EnablePrinter;

namespace internal {

template <typename T> struct IsSupportedInt : fbl::false_type {};
template <> struct IsSupportedInt<uint8_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint16_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint32_t> : fbl::true_type {};
template <> struct IsSupportedInt<uint64_t> : fbl::true_type {};

template <class IntType> constexpr IntType ComputeMask(uint32_t num_bits) {
    if (num_bits == sizeof(IntType) * CHAR_BIT) {
        return static_cast<IntType>(~0ull);
    }
    return static_cast<IntType>((static_cast<IntType>(1) << num_bits) - 1);
}

class FieldPrinter {
public:
    constexpr FieldPrinter() : name_(nullptr), bit_high_incl_(0), bit_low_(0) {
    }
    constexpr FieldPrinter(const char* name, uint32_t bit_high_incl, uint32_t bit_low)
        : name_(name), bit_high_incl_(bit_high_incl), bit_low_(bit_low) {
    }

    // Prints the field name, and the result of extracting the field from |value| in
    // hex (with a left-padding of zeroes to a length matching the maximum number of
    // nibbles needed to represent any value the field could take).
    void Print(uint64_t value, char* buf, size_t len) const;

private:
    const char* name_;
    uint32_t bit_high_incl_;
    uint32_t bit_low_;
};

// Structure used to reduce the storage cost of the pretty-printing features if
// they are not enabled.
template <typename T, typename IntType> struct FieldPrinterList {
    void AppendField(const char* name, uint32_t bit_high_incl, uint32_t bit_low) {
    }
};

template <typename IntType> struct FieldPrinterList<EnablePrinter, IntType> {
    // These two members are used for implementing the Print() function above.
    // They will typically be optimized away if Print() is not used.
    FieldPrinter fields[sizeof(IntType) * CHAR_BIT];
    unsigned num_fields = 0;

    void AppendField(const char* name, uint32_t bit_high_incl, uint32_t bit_low) {
        ZX_DEBUG_ASSERT(num_fields < fbl::count_of(fields));
        fields[num_fields++] = FieldPrinter(name, bit_high_incl, bit_low);
    }
};

// Used to record information about a field at construction time.  This enables
// checking for overlapping fields and pretty-printing.
template <class RegType> class Field {
private:
    using IntType = typename RegType::ValueType;
public:
    Field(RegType* reg, const char* name, uint32_t bit_high_incl, uint32_t bit_low) {
        IntType mask = static_cast<IntType>(
                internal::ComputeMask<IntType>(bit_high_incl - bit_low + 1) << bit_low);
        // Check for overlapping bit ranges
        ZX_DEBUG_ASSERT((reg->fields_mask_ & mask) == 0ull);
        reg->fields_mask_ = static_cast<IntType>(reg->fields_mask_ | mask);

        reg->printer_.AppendField(name, bit_high_incl, bit_low);
    }
};

// Used to record information about reserved-zero fields at construction time.
// This enables auto-zeroing of reserved-zero fields on register write.
// Represents a field that must be zeroed on write.
template <class RegType> class RsvdZField {
private:
    using IntType = typename RegType::ValueType;
public:
    RsvdZField(RegType* reg, uint32_t bit_high_incl, uint32_t bit_low) {
        IntType mask = static_cast<IntType>(
                internal::ComputeMask<IntType>(bit_high_incl - bit_low + 1) << bit_low);
        reg->rsvdz_mask_ = static_cast<IntType>(reg->rsvdz_mask_ | mask);
    }
};

// Implementation for RegisterBase::Print, see the documentation there.
// |reg_value| is the current value of the register.
// |fields_mask| is a bitmask with a bit set for each bit that has been defined
// in the register.
template <typename F>
void PrintRegister(F print_fn,
                   FieldPrinter fields[], size_t num_fields,
                   uint64_t reg_value, uint64_t fields_mask,
                   int register_width_bytes) {
    char buf[128];
    for (unsigned i = 0; i < num_fields; ++i) {
        fields[i].Print(reg_value, buf, sizeof(buf));
        print_fn(buf);
    }

    // Check if any unknown bits are set, and if so let the caller know
    uint64_t val = reg_value & ~fields_mask;
    if (val != 0) {
        int pad_len = (register_width_bytes * CHAR_BIT) / 4;
        snprintf(buf, sizeof(buf), "unknown set bits: 0x%0*" PRIx64, pad_len, val);
        buf[sizeof(buf) - 1] = 0;
        print_fn(buf);
    }
}

// Utility for the common print function of [](const char* arg) { printf("%s\n", arg); }
void PrintRegisterPrintf(FieldPrinter fields[], size_t num_fields,
                         uint64_t reg_value, uint64_t fields_mask,
                         int register_width_bytes);

} // namespace internal

} // namespace hwreg
