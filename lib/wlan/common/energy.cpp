// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <math.h>
#include <wlan/common/energy.h>

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

dBm::dBm(int8_t v) : EnergyType<int8_t, dBm>(v) {}

dBm operator+(const dBm& lhs, const dB& rhs) {
    return dBm(lhs.val + rhs.val);
}

dBm operator-(const dBm& lhs, const dB& rhs) {
    return dBm(lhs.val - rhs.val);
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

}  // namespace common
}  // namespace wlan
