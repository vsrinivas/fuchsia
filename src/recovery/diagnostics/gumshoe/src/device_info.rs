// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
pub struct DeviceInfoImpl {
    name: String,
    serial: String,
}

/// Store of "stable" Device information, like product name and serial.
/// Used as a base for data passed to Handlebars template renderer.
impl DeviceInfoImpl {
    pub fn new() -> Self {
        DeviceInfoImpl { name: "DEVICE NAME".to_string(), serial: "DEVICE SERIAL".to_string() }
    }
}

#[cfg(test)]
mod tests {
    use crate::device_info::DeviceInfoImpl;

    #[test]
    /// Verifies placeholder implementation of DeviceInfo sets name/serial.
    fn placeholder_device_info_has_values() {
        let device = DeviceInfoImpl::new();

        assert_eq!("DEVICE NAME", device.name);
        assert_eq!("DEVICE SERIAL", device.serial);
    }
}
