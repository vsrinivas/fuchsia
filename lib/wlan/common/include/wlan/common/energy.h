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

template <typename Storage, typename Unit> struct EnergyType {
    Storage val;

   protected:
    explicit EnergyType(Storage v) : val(v) {}
};

// Below operators are defined only for the operands with matching template types.
template <typename Storage, typename Unit>
bool operator==(const EnergyType<Storage, Unit>& lhs, const EnergyType<Storage, Unit>& rhs) {
    return lhs.val == rhs.val;
}

template <typename Storage, typename Unit>
bool operator!=(const EnergyType<Storage, Unit>& lhs, const EnergyType<Storage, Unit>& rhs) {
    return lhs.val != rhs.val;
}

template <typename Storage, typename Unit>
bool operator>(const EnergyType<Storage, Unit>& lhs, const EnergyType<Storage, Unit>& rhs) {
    return lhs.val > rhs.val;
}

template <typename Storage, typename Unit>
bool operator<(const EnergyType<Storage, Unit>& lhs, const EnergyType<Storage, Unit>& rhs) {
    return lhs.val < rhs.val;
}

template <typename Storage, typename Unit>
bool operator>=(const EnergyType<Storage, Unit>& lhs, const EnergyType<Storage, Unit>& rhs) {
    return lhs.val >= rhs.val;
}

template <typename Storage, typename Unit>
bool operator<=(const EnergyType<Storage, Unit>& lhs, const EnergyType<Storage, Unit>& rhs) {
    return lhs.val <= rhs.val;
}

// milliwatt. 10^(-3) Watt.
struct mWatt : EnergyType<uint16_t, mWatt> {
    explicit mWatt(uint16_t v = 0);
};

// IEEE Std 802.11-2016, Table 9-60, 9-71
// For the use for SNR or relative comparison.
// For precision of 1 dB step, See IEEE 802.11-2016, Table 6-7, 9-18, etc.
struct dB : EnergyType<int8_t, dB> {
    explicit dB(int8_t v = 0);
};

// For precision of 0.5 dB step, See IEEE 802.11-2016, 9.4.2.41, 9.4.2.162,
struct dBh : EnergyType<int16_t, dBh> {
    explicit dBh(int16_t v = 0);
};

// TODO(porce): Implement dBq unit
// For precision of 0.25 dB step, See IEEE 802.11-2016, 9.4.1.28-30, 9.4.1.49, Table 20-1

// TODO(porce): Implement dBr unit
// IEEE 802.11-2016, 20.3.2

// For ANPI, IEEE Std 802.11-2016, 11.11.9.4,
// DataFrameRSSI, IEEE Std 802.11-2016, Table 6-7
// Beacon RSSI IEEE Std 802.11-2016, 11.45, Table 6-7
// Note, RXVECTOR's RSSI is unitless uint8_t.
// For Transmit Power
// See dot11MaximumTransmitPowerLevel, defined as int32_t
struct dBm : EnergyType<int8_t, dBm> {
    explicit dBm(int8_t v = 0);
};

// IEEE Std 802.11-2016, 9.4.2.21.7, 9.4.2.38, 9.6.8.30, etc.
struct dBmh : EnergyType<int16_t, dBmh> {
    explicit dBmh(int16_t v = 0);
};

dB to_dB(dBh u);
dBh to_dBh(dB u);
dBm to_dBm(dBmh u);
dBmh to_dBmh(dBm u);

// IEEE Std 802.11-2016, 9.5.1.19-20, Figure 9-391 Tx Power field
// TODO(porce): Implement int8_t dBmTwosComplement(dBm val);
typedef uint8_t Rcpi;  // IEEE Std 802.11-2016, Table 9-154
Rcpi to_Rcpi(dBmh u, bool measured);

mWatt operator+(const mWatt& lhs, const mWatt& rhs);
mWatt operator-(const mWatt& lhs, const mWatt& rhs);
mWatt operator-(const mWatt& rhs);

dB operator+(const dB& lhs, const dB& rhs);
dB operator-(const dB& lhs, const dB& rhs);
dB operator-(const dB& rhs);
dBh operator+(const dBh& lhs, const dBh& rhs);
dBh operator-(const dBh& lhs, const dBh& rhs);
dBh operator-(const dBh& rhs);
dBm operator+(const dBm& lhs, const dB& rhs);
dBm operator-(const dBm& lhs, const dB& rhs);
dBmh operator+(const dBmh& lhs, const dBh& rhs);
dBmh operator-(const dBmh& lhs, const dBh& rhs);

}  // namespace common
}  // namespace wlan
