// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use serde::Deserialize;
use std::str::FromStr;

pub enum Device2Method {
    Transfer,
}

impl FromStr for Device2Method {
    type Err = anyhow::Error;
    fn from_str(method: &str) -> Result<Self, Self::Err> {
        match method {
            "Transfer" => Ok(Device2Method::Transfer),
            _ => Err(format_err!("'{}' is not a valid Device2Method", method)),
        }
    }
}

#[derive(Deserialize, Debug)]
pub struct TransferRequest {
    pub device_idx: u32,
    pub segments_is_write: Vec<bool>,
    pub write_segments_data: Vec<Vec<u8>>,
    pub read_segments_length: Vec<u8>,
}
