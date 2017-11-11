// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

namespace wlan {

// TODO(porce): struct Channel: Reconcile with other channel structs.
struct Channel {
    // Nomenclature:
    // Each field below is a channel number defined in [0, 200].
    // 20, 40, 80 are bandwidth in MHz.
    // For the bandwidth greater than 20MHz, a channel number in that bandwidth
    // means the lowest channel number of that bandwidth.

    // Used both in 2GHz and 5GHz.
    uint8_t primary20;
    uint8_t secondary20;  // Defines primary40 together with primary40.
                          // This abolishes 802.11n 40MHz channelization
                          // of format in IEEE Std 802.11-2016, 19.3.15.4

    // Used only in 5GHz.
    uint8_t secondary40;  // Defines primary80 together with primary80.
    uint8_t secondary80;  // Defines primary160 or 80+80 together with primary80.

    static constexpr uint8_t kUnspecified = 0;

    /*  TODO(porce): Uncomment this upon adding other helper functions.
    bool Is2ghz() {
        // See IEEE Std 802.11-2016 19.3.15.2
        return (1 <= primary20 && primary20 <= 13);
    }
    bool Is5ghz() {
        // See IEEE Std 802.11-2016 19.3.15.3. Possible spec error?
        return (!Is2ghz() && 1 <= primary20 && primary20 <= 200);
        // return (36 <= primary20 && primary20 <= 173);  // More practical.
    }
    */

    // TODO(porce): Bandwidth.
    // TODO(porce): Notation string.
    // TODO(porce): Center frequencies.
    // Define the rule to translsate center frequency to/from channel numbering.
    // See IEEE Std 802.11-2016 19.3.15
};

}  // namespace wlan
