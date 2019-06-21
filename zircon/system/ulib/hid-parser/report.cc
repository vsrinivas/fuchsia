// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid-parser/report.h>
#include <hid-parser/units.h>

#include <type_traits>

namespace hid {
namespace {

// This sign extends the |n_bits| of |data| and returns it as a full
// int32_t value. |n_bits| must be between [0, 31].
// Example: If the user had a 2's complement 5 bit number 0b11111 it
// would represent -1. To sign extend this to an int32_t the function
// would be called as SignExtendFromBits(0b11111, 5)
constexpr int32_t SignExtendFromNBits(uint32_t data, int32_t n_bits) {
    // Expression taken and simplified from:
    // http://graphics.stanford.edu/~seander/bithacks.html#VariableSignExtend

    // Zero out information about n_bits.
    data = data & ((1U << n_bits) - 1);
    // Do the sign extend.
    int32_t msb = 1U << (n_bits - 1);
    return static_cast<int32_t>((data ^ msb) - msb);
}

inline bool FieldFits(size_t report_len, const Attributes& attr) {
    return (attr.offset + attr.bit_sz) <= (report_len * 8);
}

// Extracts bits from a byte and returns them.
// |begin| is the starting bit: it starts at 0 and counts from LSB to MSB.
// The bits are placed at the beginning of return value.
// Make sure when this is called that (|begin| + |count|) <= 8.
// Example: ExtractBitsFromByte(1b00010100, 2, 3) returns 1b101.
inline uint8_t ExtractBitsFromByte(uint8_t val, uint32_t begin, uint8_t count) {
    uint8_t mask = static_cast<uint8_t>((0xFF >> (8 - count)) << begin);
    return static_cast<uint8_t>((val & mask) >> begin);
}

// Create a mask of set bits of a given |size| starting at |start_bit|.
// Eg. create_mask(2, 3) = 1b11100;
constexpr uint32_t create_mask(uint32_t start_bit, uint32_t size) {
    return (~((~0U) << (size))) << start_bit;
}

} // namespace

#define MIN(a, b) (a) < (b) ? (a) : (b)
template <typename T>
bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 T* value_out) {
    static_assert(std::is_pod<T>::value, "not POD");
    if (attr.bit_sz > sizeof(T) * 8) {
        return false;
    }
    if (!FieldFits(report_len, attr)) {
        return false;
    }
    T val = 0;

    const uint32_t start_bit = attr.offset;
    const uint32_t end_bit = start_bit + attr.bit_sz;
    uint32_t index_bit = attr.offset;
    while (index_bit < end_bit) {
        uint8_t bits_till_byte_end = static_cast<uint8_t>(8u - (index_bit % 8u));
        uint8_t bit_count = static_cast<uint8_t>(MIN(bits_till_byte_end, end_bit - index_bit));

        uint8_t extracted_bits =
            ExtractBitsFromByte(report[index_bit / 8u], index_bit % 8u, bit_count);

        val = static_cast<T>(val | (extracted_bits << (index_bit - start_bit)));

        index_bit += bit_count;
    }

    *value_out = val;
    return true;
}

bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 uint8_t* value_out) {
    return ExtractUint<uint8_t>(report, report_len, attr, value_out);
}

bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 uint16_t* value_out) {
    return ExtractUint<uint16_t>(report, report_len, attr, value_out);
}

bool ExtractUint(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                 uint32_t* value_out) {
    return ExtractUint<uint32_t>(report, report_len, attr, value_out);
}

bool ExtractAsUnit(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                   double* value_out) {
    if (value_out == nullptr) {
        return false;
    }

    uint32_t uint_out;
    bool ret = ExtractUint(report, report_len, attr, &uint_out);
    if (!ret) {
        return false;
    }

    // If the minimum value is less than zero, then the maximum
    // value and the value itself are an unsigned number. Otherwise they
    // are signed numbers.
    const int64_t logc_max =
        (attr.logc_mm.min < 0) ? attr.logc_mm.max : static_cast<uint32_t>(attr.logc_mm.max);
    int64_t phys_max =
        (attr.phys_mm.min < 0) ? attr.phys_mm.max : static_cast<uint32_t>(attr.phys_mm.max);
    double val = (attr.logc_mm.min < 0)
                     ? static_cast<double>(SignExtendFromNBits(uint_out, attr.bit_sz))
                     : uint_out;

    if (val < static_cast<double>(attr.logc_mm.min) || val > static_cast<double>(attr.logc_mm.max)) {
        return false;
    }

    int64_t phys_min = attr.phys_mm.min;
    if (phys_max == 0 && phys_min == 0) {
        phys_min = attr.logc_mm.min;
        phys_max = logc_max;
    }

    double resolution =
        static_cast<double>(logc_max - attr.logc_mm.min) / static_cast<double>(phys_max - phys_min);
    *value_out = val / resolution;

    return true;
}

bool ExtractWithUnit(const uint8_t* report, size_t report_len, const hid::Attributes& attr,
                     const Unit& unit_out, double* value_out) {
    double val = 0;
    if (!ExtractAsUnit(report, report_len, attr, &val)) {
        return false;
    }

    return unit::ConvertUnits(attr.unit, val, unit_out, value_out);
}

bool InsertUint(uint8_t* report, size_t report_len, const hid::Attributes& attr, uint32_t value_in) {
    if (attr.bit_sz > sizeof(int32_t) * 8) {
        return false;
    }
    if (!(FieldFits(report_len, attr))) {
        return false;
    }

    const uint32_t start_bit = attr.offset;
    const uint32_t end_bit = start_bit + attr.bit_sz;
    uint32_t index_bit = attr.offset;
    // Fill in the data from index bit to end bit, going at most a full byte at
    // a time. For the first or the last byte there could be less than a full
    // byte.
    while (index_bit < end_bit) {
        uint8_t bits_from_byte_start = index_bit % 8;
        uint8_t bits_till_byte_end = static_cast<uint8_t>(8u - (bits_from_byte_start));
        uint8_t bit_count = static_cast<uint8_t>(MIN(bits_till_byte_end, end_bit - index_bit));

        // Get the bits from value_in.
        uint32_t value_in_start_bit = index_bit - start_bit;
        uint32_t value_in_bits = (value_in & create_mask(value_in_start_bit, bit_count)) >> value_in_start_bit;
        uint8_t value = static_cast<uint8_t>(value_in_bits << bits_from_byte_start);

        // Get the bits from the report data.
        uint8_t data_mask = static_cast<uint8_t>(~create_mask(bits_from_byte_start, bit_count));
        value |= report[index_bit / 8] & data_mask;

        report[index_bit / 8] = value;

        index_bit += bit_count;
    }

    return true;
}

bool InsertAsUnit(uint8_t* report, size_t report_len, const hid::Attributes& attr, double value_in) {
    // If the minimum value is less than zero, then the maximum
    // value and the value itself are an unsigned number. Otherwise they
    // are signed numbers.
    const int64_t logc_max =
        (attr.logc_mm.min < 0) ? attr.logc_mm.max : static_cast<uint32_t>(attr.logc_mm.max);
    int64_t phys_max =
        (attr.phys_mm.min < 0) ? attr.phys_mm.max : static_cast<uint32_t>(attr.phys_mm.max);
    int64_t phys_min = attr.phys_mm.min;

    if (phys_max == 0 && phys_min == 0) {
        phys_min = attr.logc_mm.min;
        phys_max = logc_max;
    }

    if (value_in < static_cast<double>(phys_min) || value_in > static_cast<double>(phys_max)) {
        return false;
    }

    double resolution =
        static_cast<double>(logc_max - attr.logc_mm.min) / static_cast<double>(phys_max - phys_min);
    value_in = value_in * resolution;

    // Do a conversion to int32_t and reinterpret it into a uint32_t while keeping all bits the
    // same. Without this some negative numbers get converted to 0.
    int32_t signed_value_in = static_cast<int32_t>(value_in);
    uint32_t value_in_bytes = *reinterpret_cast<uint32_t*>(&signed_value_in);

    return InsertUint(report, report_len, attr, value_in_bytes);
}

bool InsertWithUnit(uint8_t* report, size_t report_len, const hid::Attributes& attr,
                    const Unit& unit_in, double value_in) {
    double value_converted;
    if (!unit::ConvertUnits(unit_in, value_in, attr.unit, &value_converted)) {
        return false;
    }

    return InsertAsUnit(report, report_len, attr, value_converted);
}

#undef MIN
} // namespace hid
