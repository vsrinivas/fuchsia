// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::indexes::StreamConfigIndex, anyhow::Error, std::collections::HashMap};

#[derive(Eq, Hash, PartialEq, Debug)]
pub struct Device {
    /// Device manufacturer name.
    pub manufacturer: String,

    /// Device product name.
    pub product: String,

    /// Is codec or DAI.
    pub is_codec: bool,

    /// DAI channel for instance TDM slot.
    pub dai_channel: u8,
}

#[derive(Default)]
pub struct Config {
    /// Indexes to the available StreamConfigs.
    pub stream_config_indexes: HashMap<Device, StreamConfigIndex>,
}

impl Config {
    pub fn new() -> Result<Self, Error> {
        Ok(Self { stream_config_indexes: HashMap::new() })
    }

    #[cfg(test)]
    pub fn load_device(&mut self, device: Device, index: StreamConfigIndex) {
        self.stream_config_indexes.insert(device, index);
    }
}
