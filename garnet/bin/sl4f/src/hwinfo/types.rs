// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hwinfo::DeviceInfo;
use serde_derive::Serialize;

#[derive(Clone, Debug, Serialize)]
pub struct SerializableDeviceInfo {
    pub serial_number: Option<String>,
}

/// DeviceInfo object is not serializable so serialize the object.
impl SerializableDeviceInfo {
    pub fn new(device: &DeviceInfo) -> Self {
        SerializableDeviceInfo { serial_number: device.serial_number.clone() }
    }
}
