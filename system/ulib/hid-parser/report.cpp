// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/type_support.h>

#include <hid-parser/report.h>

namespace hid {
namespace {
inline bool FieldFits(const Report& report, const Attributes& attr) {
    return (attr.offset + attr.bit_sz) <= (report.len * 8);
}

// Extracts bits from a byte and returns them
// Begin is the starting bit: it starts at 0 and counts from LSB to MSB
// The bits are placed at the beginning of return value.
// Make sure when this is called that (begin + count) <= 8
// Eg: ExtractBitsFromByte(1b00010100, 2, 3) returns 1b101
inline uint8_t ExtractBitsFromByte(uint8_t val, uint32_t begin, uint8_t count) {
    uint8_t mask = static_cast<uint8_t>((0xFF >> (8 - count)) << begin);
    return static_cast<uint8_t>((val & mask) >> begin);
}
} // namespace

#define MIN(a, b) (a) < (b) ? (a) : (b)
template <typename T>
bool ExtractUint(const Report& report, const hid::Attributes& attr, T* value_out) {
    static_assert(fbl::is_pod<T>::value, "not POD");
    if (attr.bit_sz > sizeof(T) * 8) {
        return false;
    }
    if (!FieldFits(report, attr)) {
        return false;
    }
    T val = 0;

    const uint32_t start_bit = attr.offset;
    const uint32_t end_bit = start_bit + attr.bit_sz;
    uint32_t index_bit = attr.offset;
    while (index_bit < end_bit) {
        uint8_t bits_till_byte_end = static_cast<uint8_t>(8u - (index_bit % 8u));
        uint8_t bit_count = static_cast<uint8_t>(MIN(bits_till_byte_end, end_bit - index_bit));

        uint8_t extracted_bits = ExtractBitsFromByte(report.data[index_bit / 8u], index_bit % 8u, bit_count);

        val = static_cast<T>(val | (extracted_bits << (index_bit - start_bit)));

        index_bit += bit_count;
    }

    *value_out = val;
    return true;
}
#undef MIN

bool ExtractUint(const Report& report, const hid::Attributes& attr, uint8_t* value_out) {
    return ExtractUint<uint8_t>(report, attr, value_out);
}

bool ExtractUint(const Report& report, const hid::Attributes& attr, uint16_t* value_out) {
    return ExtractUint<uint16_t>(report, attr, value_out);
}

bool ExtractUint(const Report& report, const hid::Attributes& attr, uint32_t* value_out) {
    return ExtractUint<uint32_t>(report, attr, value_out);
}

} // namespace hid
