// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <string>

// Energy units defined here are to represent those defined in
// IEEE standards and International System of Units.
// The upper/lower cases in units are of significant meaning.
// Do not alter cases to meet a particular coding style.

// Presentation factors in following considerations:
// 1. Valid range as in defined in the standards.
// 2. Imperative precision of values (integral, 0.5 step, 0.25 step, etc.).
// 3. Practical precision of values (up to the hardware).
// 4. Storage size
// 5. Cross-language compatibility
// 6. Encoding schemes defined in the standards.

// Consider float (32 bits) or fixed-point type (8 or 16 bits)
// when integral type does not meet needs.

namespace wlan {
namespace common {

typedef uint16_t mWatt;  // milliwatts. 10^(-3) Watt

// IEEE Std 802.11-2016, Table 9-60, 9-71
// For the use for SNR or relative comparison.
// For precision of 1 dB step, See IEEE 802.11-2016, Table 6-7, 9-18, etc.
typedef int8_t dB;  // 10 * log10(a ratio). Unitless ratio of two numbers.

// For precision of 0.5 dB step, See IEEE 802.11-2016, 9.4.2.41, 9.4.2.162,
typedef int8_t dBh;  // dB encoded in half (0.5) dB step. Fuchsia unit.

// For precision of 0.25 dB step, See IEEE 802.11-2016, 9.4.1.28-30, 9.4.1.49, Table 20-1
typedef int16_t dBq;  // dB encoded in quarter (0.25) dB step. Fuchsia unit.

typedef int8_t dBr;  // IEEE 802.11-2016, 20.3.2

// For ANPI, IEEE Std 802.11-2016, 11.11.9.4,
// DataFrameRSSI, IEEE Std 802.11-2016, Table 6-7
// Beacon RSSI IEEE Std 802.11-2016, 11.45, Table 6-7
// Note, RXVECTOR's RSSI is unitless uint8_t.
// For Transmit Power
// See dot11MaximumTransmitPowerLevel, defined as int32_t
typedef int8_t dBm;  // 10 * log10(mWatt)

// IEEE Std 802.11-2016, 9.4.2.21.7, 9.4.2.38, 9.6.8.30, etc.
typedef uint16_t dBmh;  // dBm encoded in half (0.5) dB step. Fuchsia unit.

typedef uint16_t dBmq;  // dBm encoded in quarter (0.25) dB step. Fuchsia unit.

typedef uint8_t Rcpi;  // IEEE Std 802.11-2016, Table 9-154

dBm to_dBm(mWatt val) {
    return static_cast<dBm>(10.0 * log10(static_cast<float>(val)));
}

mWatt to_mWatt(dBm val) {
    return static_cast<mWatt>(pow(10.0f, static_cast<float>(val) / 10.0f));
}

dBm add_dbm(dBm val1, dBm val2) {
    dBm max = std::max(val1, val2);
    dBm min = std::min(val1, val2);

    constexpr dBm kBigGap = 9;  // Corresponds to 0.51 dB addition
    if (max - min >= kBigGap) {
        // the smaller one is negligible
        return max;
    }

    float alpha = pow(10.0f, static_cast<float>((min - max) / 10.0f));
    float beta = static_cast<dBm>(10.0f * log10(1 + alpha));
    return max + beta;
}

dBm subtract_dbm(dBm val1, dBm val2) {
    // Note, this is different from (dBm - dB) operation,
    // which is naturally defined without restrictions on values.
    ZX_DEBUG_ASSERT(val1 >= val2);  // Negative RF energy is yet to be defined.

    dBm max = std::max(val1, val2);
    dBm min = std::min(val1, val2);

    constexpr dBm kBigGap = 9;  // Corresponds to 0.51 dB addition
    if (max - min >= kBigGap) {
        // the smaller one is negligible
        return max;
    }

    float alpha = pow(10.0f, static_cast<float>((min - max) / 10.0f));
    float beta = static_cast<dBm>(10.0f * log10(1 - alpha));
    return max + beta;
}

// IEEE Std 802.11-2016, 9.5.1.19-20, Figure 9-391 Tx Power field
// TODO(porce): Implement int8_t dBmTwosComplement(dBm val);

Rcpi ToRcpi(dBm val, bool measured = true) {
    if (!measured) { return 255; }
    if (val < -109.5) { return 0; }
    if (val >= 0) { return 220; }

    return static_cast<Rcpi>(2 * (val + 110));
}

}  // namespace common
}  // namespace wlan
