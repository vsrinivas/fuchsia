// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use {fidl_fuchsia_wlan_common as fidl_common, rand::Rng as _, std::convert::TryInto as _};

pub fn generate_random_channel() -> fidl_common::WlanChan {
    let mut rng = rand::thread_rng();
    generate_channel(rng.gen::<u8>())
}

pub fn generate_channel(channel: u8) -> fidl_common::WlanChan {
    let mut rng = rand::thread_rng();
    fidl_common::WlanChan {
        primary: channel,
        cbw: match rng.gen_range(0, 5) {
            0 => fidl_common::Cbw::Cbw20,
            1 => fidl_common::Cbw::Cbw40,
            2 => fidl_common::Cbw::Cbw40Below,
            3 => fidl_common::Cbw::Cbw80,
            4 => fidl_common::Cbw::Cbw160,
            5 => fidl_common::Cbw::Cbw80P80,
            _ => panic!(),
        },
        secondary80: rng.gen::<u8>(),
    }
}

pub fn generate_random_bss_desc() -> Option<Box<fidl_fuchsia_wlan_internal::BssDescription>> {
    let mut rng = rand::thread_rng();
    Some(Box::new(fidl_fuchsia_wlan_internal::BssDescription {
        bssid: (0..6).map(|_| rng.gen::<u8>()).collect::<Vec<u8>>().try_into().unwrap(),
        bss_type: fidl_fuchsia_wlan_internal::BssTypes::Personal,
        beacon_period: rng.gen::<u16>(),
        timestamp: rng.gen::<u64>(),
        local_time: rng.gen::<u64>(),
        cap: rng.gen::<u16>(),
        ies: (0..1024).map(|_| rng.gen::<u8>()).collect(),
        rssi_dbm: rng.gen::<i8>(),
        chan: generate_random_channel(),
        snr_db: rng.gen::<i8>(),
    }))
}
