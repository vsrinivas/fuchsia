// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Enum for supported FIDL commands, to extend support for new commands, add to this definition,
/// update ble_method_to_fidl, and implement helper methods in BluetoothFacade
pub enum BluetoothMethod {
    BleAdvertise,
    BleConnectPeripheral,
    BleScan,
    BleStopAdvertise,
    BleUndefined,
}

impl BluetoothMethod {
    pub fn from_str(method: String) -> BluetoothMethod {
        match method.as_ref() {
            "BleScan" => BluetoothMethod::BleScan,
            "BleAdvertise" => BluetoothMethod::BleAdvertise,
            "BleStopAdvertise" => BluetoothMethod::BleStopAdvertise,
            "BleConnectPeripheral" => BluetoothMethod::BleConnectPeripheral,
            _ => BluetoothMethod::BleUndefined,
        }
    }
}

/// BleScan result type
/// TODO(aniramakri): Add support for RemoteDevices when clone() is implemented
#[derive(Serialize, Clone, Debug)]
pub struct BleScanResponse {
    pub id: String,
    pub name: String,
    pub connectable: bool,
}

impl BleScanResponse {
    pub fn new(id: String, name: String, connectable: bool) -> BleScanResponse {
        BleScanResponse {
            id,
            name,
            connectable,
        }
    }
}

/// BleAdvertise result type (only uuid)
/// TODO(aniramakri): Add support for AdvertisingData when clone() is implemented
#[derive(Serialize, Clone, Debug)]
pub struct BleAdvertiseResponse {
    pub name: Option<String>,
}

impl BleAdvertiseResponse {
    pub fn new(name: Option<String>) -> BleAdvertiseResponse {
        BleAdvertiseResponse { name }
    }
}
