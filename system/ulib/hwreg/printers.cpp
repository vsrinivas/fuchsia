// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/internal.h>
#include <stdio.h>

namespace hwreg {

namespace internal {

void FieldPrinter::Print(uint64_t value, char* buf, size_t len) const {
    unsigned num_bits = bit_high_incl_ - bit_low_ + 1;
    uint64_t mask = internal::ComputeMask<uint64_t>(num_bits);
    uint64_t val = static_cast<uint64_t>((value >> bit_low_) & mask);
#ifdef _KERNEL
    // The kernel does not support the * directive in printf, so we lose out on
    // the length-matching padding.
    snprintf(buf, len, "%s[%u:%u]: 0x%" PRIx64 " (%" PRIu64 ")", name_,
             bit_high_incl_, bit_low_, val, val);
#else
    int pad_len = (num_bits + 3) / 4;
    snprintf(buf, len, "%s[%u:%u]: 0x%0*" PRIx64 " (%" PRIu64 ")", name_,
             bit_high_incl_, bit_low_, pad_len, val, val);
#endif // _KERNEL
    buf[len - 1] = 0;
}

void PrintRegisterPrintf(FieldPrinter fields[], size_t num_fields,
                         uint64_t reg_value, uint64_t fields_mask,
                         int register_width_bytes) {
    PrintRegister([](const char* arg) { printf("%s\n", arg); },
                  fields, num_fields, reg_value, fields_mask, register_width_bytes);
}

} // namespace internal

} // namespace hwreg
