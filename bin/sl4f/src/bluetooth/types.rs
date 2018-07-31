// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_gatt::ServiceInfo;

/// Enum for supported FIDL commands, to extend support for new commands, add to this
/// definition, update ble_method_to_fidl, and implement helper methods in BluetoothFacade
pub enum BluetoothMethod {
    BleAdvertise,
    BleConnectPeripheral,
    BleDisconnectPeripheral,
    BleListServices,
    BleScan,
    BleStopAdvertise,
    BleUndefined,
}

impl BluetoothMethod {
    pub fn from_str(method: String) -> BluetoothMethod {
        match method.as_ref() {
            "BleAdvertise" => BluetoothMethod::BleAdvertise,
            "BleConnectPeripheral" => BluetoothMethod::BleConnectPeripheral,
            "BleDisconnectPeripheral" => BluetoothMethod::BleDisconnectPeripheral,
            "BleListServices" => BluetoothMethod::BleListServices,
            "BleScan" => BluetoothMethod::BleScan,
            "BleStopAdvertise" => BluetoothMethod::BleStopAdvertise,
            _ => BluetoothMethod::BleUndefined,
        }
    }
}

/// BleScan result type
/// TODO(NET-1026): Add support for RemoteDevices when clone() is implemented
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
/// TODO(NET-1026): Add support for AdvertisingData when clone() is implemented
#[derive(Serialize, Clone, Debug)]
pub struct BleAdvertiseResponse {
    pub name: Option<String>,
}

impl BleAdvertiseResponse {
    pub fn new(name: Option<String>) -> BleAdvertiseResponse {
        BleAdvertiseResponse { name }
    }
}

/// BleConnectPeripheral response (aka ServiceInfo)
/// TODO(NET-1026): Add support for ServiceInfo when clone(), serialize(), derived
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct BleConnectPeripheralResponse {
    pub id: u64,
    pub primary: bool,
    pub uuid_type: String,
}

impl BleConnectPeripheralResponse {
    pub fn new(info: Vec<ServiceInfo>) -> Vec<BleConnectPeripheralResponse> {
        let mut res = Vec::new();
        for v in info {
            let copy = BleConnectPeripheralResponse {
                id: v.id,
                primary: v.primary,
                uuid_type: v.type_,
            };
            res.push(copy)
        }
        res
    }
}
