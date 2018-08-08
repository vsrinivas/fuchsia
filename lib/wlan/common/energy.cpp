// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <wlan/common/energy.h>
#include <limits>

namespace wlan {
namespace common {

mWatt::mWatt(uint16_t v) : EnergyType<uint16_t, mWatt>(v) {}

mWatt operator+(const mWatt& lhs, const mWatt& rhs) {
    return mWatt(lhs.val + rhs.val);
}

mWatt operator-(const mWatt& lhs, const mWatt& rhs) {
    return mWatt(lhs.val - rhs.val);
}

mWatt operator-(const mWatt& rhs) {
    return mWatt(-rhs.val);
}

dB::dB(int8_t v) : EnergyType<int8_t, dB>(v) {}

dB operator+(const dB& lhs, const dB& rhs) {
    return dB(lhs.val + rhs.val);
}

dB operator-(const dB& lhs, const dB& rhs) {
    return dB(lhs.val - rhs.val);
}

dB operator-(const dB& rhs) {
    return dB(-rhs.val);
}

dBh::dBh(int16_t v) : EnergyType<int16_t, dBh>(v) {}

dBh operator+(const dBh& lhs, const dBh& rhs) {
    return dBh(lhs.val + rhs.val);
}

dBh operator-(const dBh& lhs, const dBh& rhs) {
    return dBh(lhs.val - rhs.val);
}

dBh operator-(const dBh& rhs) {
    return dBh(-rhs.val);
}

dBm operator+(const dBm& lhs, const dB& rhs) {
    return dBm(lhs.val + rhs.val);
}

dBm operator-(const dBm& lhs, const dB& rhs) {
    return dBm(lhs.val - rhs.val);
}

static dBm add_dBm(const dBm& lhs, const dBm& rhs) {
    auto max = std::max(lhs.val, rhs.val);
    auto min = std::min(lhs.val, rhs.val);
    auto diff = max - min;

    // Math formula for the answer. Note the answer is a function of diff.
    // alpha := pow(10.0f, -diff / 10.0f);
    // beta := 10.0f * log10(1 + alpha);
    // answer := max + beta;

    // Since dBm is an integral type, it can be quantized to the integer precision.

    int8_t beta = 0;
    switch (diff) {
    case 0:
    case 1:
        beta = 3;
        break;
    case 2:
    case 3:
        beta = 2;
        break;
    case 4:
    case 5:
    case 6:
    case 7:
    case 8:
    case 9:
        beta = 1;
        break;
    default:
        beta = 0;
        break;
    }
    return dBm(max + beta);
}

dBm operator+(const dBm& lhs, const dBm& rhs) {
    return add_dBm(lhs, rhs);
}

dBmh::dBmh(int16_t v) : EnergyType<int16_t, dBmh>(v) {}

dBmh operator+(const dBmh& lhs, const dBh& rhs) {
    return dBmh(lhs.val + rhs.val);
}

dBmh operator-(const dBmh& lhs, const dBh& rhs) {
    return dBmh(lhs.val - rhs.val);
}

dB to_dB(dBh u) {
    return dB(u.val / 2);
}

dBh to_dBh(dB u) {
    return dBh(u.val * 2);
}

dBm to_dBm(dBmh u) {
    return dBm(u.val / 2);
}

dBmh to_dBmh(dBm u) {
    return dBmh(u.val * 2);
}

// TODO(porce): Implement int8_t dBmTwosComplement(dBm val);

// IEEE Std 802.11-2016, Table 9-154
Rcpi to_Rcpi(dBmh u, bool measured) {
    if (!measured) { return 255; }
    if (u < dBmh(-219)) { return 0; }  // -219 = 2 * (-109.5)
    if (u.val >= 0) { return 220; }

    return static_cast<uint8_t>(u.val + 220);  // 2 * (dBm + 100)
}

dBm to_dBm(FemtoWatt fw) {
    if (fw.val == 0) { return dBm(std::numeric_limits<int8_t>::min()); }
    // This shouldn't be called too often, so we use log10() to make it simple.
    double dbm = 10.0 * (log10(static_cast<double>(fw.val)) - 12.0);
    // Casting to int8_t is safe since the resulting value will always be between
    // -120 and 73 for all possible uint64_t inputs
    return dBm(static_cast<int8_t>(round(dbm)));
}

}  // namespace common
}  // namespace wlan
