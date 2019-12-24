// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fuchsia_bluetooth::{
        error::Error as BtError,
        types::{Address, PeerId},
    },
};

pub const BONDING_DATA_PREFIX: &'static str = "bonding-data:";
pub const HOST_DATA_PREFIX: &'static str = "host-data:";

pub fn bonding_data_key(device_id: PeerId) -> String {
    format!("{}{}", BONDING_DATA_PREFIX, device_id)
}

pub fn host_data_key(host_address: &Address) -> String {
    format!("{}{}", HOST_DATA_PREFIX, host_address.to_string())
}

pub fn host_id_from_key(key: &str) -> Result<String, Error> {
    if key.len() <= HOST_DATA_PREFIX.len() {
        return Err(BtError::new(&format!("malformed host data key: {}", key)).into());
    }
    Ok(key[HOST_DATA_PREFIX.len()..].to_string())
}
