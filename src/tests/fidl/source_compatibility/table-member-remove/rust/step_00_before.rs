// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use fidl_fidl_test_tablememberremove as fidl_lib;

// [START contents]
fn use_table(profile: &fidl_lib::Profile) {
    if let Some(tz) = &profile.timezone {
        println!("timezone: {:?}", tz);
    }
    if let Some(unit) = &profile.temperature_unit {
        println!("preferred unit: {:?}", unit);
    }
    if let Some(is_on) = &profile.dark_mode {
        println!("dark mode on: {}", is_on);
    }
    if let Some(data) = &profile.unknown_data {
        for (ordinal, bytes) in data.iter() {
            println!("unknown ordinal {} with bytes {:?}", ordinal, bytes);
        }
    }
}
// [END contents]

fn main() {}
