// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const BONDING_DATA_PREFIX: &'static str = "bonding-data:";

pub fn bonding_data_key(device_id: &str) -> String {
    format!("{}{}", BONDING_DATA_PREFIX, device_id)
}
