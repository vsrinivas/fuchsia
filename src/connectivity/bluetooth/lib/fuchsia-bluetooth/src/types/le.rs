// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module declares native Rust encodings equivalent to FIDL structs for the
//! Bluetooth LowEnergy interfaces. These structs use standard Rust primitives
//! rather than the default mapping from FIDL, and derive the `Clone` trait for a
//! more ergonomic api than those exposed in the `fidl_fuchsia_bluetooth_le`
//! crate.
//!
//! These types also implement the `From` trait, so usage when receiving a fidl
//! struct is simply a case of calling `.into(...)` (`From` implies `Into`):
//!
//! ```ignore
//!   fn use_peer(fidl_peer: fidl_fuchsia_bluetooth_le::RemoteDevice) {
//!      let peer: Le::RemoteDevice = fidl_peer.into();
//!      ...
//!   }
//! ```

use fidl_fuchsia_bluetooth_le as fidl;

#[derive(Clone)]
pub struct RemoteDevice {
    pub identifier: String,
    pub connectable: bool,
    pub rssi: Option<i8>,
    pub advertising_data: Option<AdvertisingData>,
}

#[derive(Clone)]
pub struct AdvertisingData {
    pub name: Option<String>,
    pub tx_power_level: Option<i8>,
    pub appearance: Option<u16>,
    pub service_uuids: Vec<String>,
    pub service_data: Vec<ServiceDataEntry>,
    pub manufacturer_specific_data: Vec<ManufacturerSpecificDataEntry>,
    pub solicited_service_uuids: Vec<String>,
    pub uris: Vec<String>,
}

#[derive(Clone)]
pub struct ServiceDataEntry {
    pub uuid: String,
    pub data: Vec<u8>,
}

#[derive(Clone)]
pub struct ManufacturerSpecificDataEntry {
    pub company_id: u16,
    pub data: Vec<u8>,
}

impl From<fidl::RemoteDevice> for RemoteDevice {
    fn from(src: fidl::RemoteDevice) -> RemoteDevice {
        RemoteDevice {
            identifier: src.identifier,
            connectable: src.connectable,
            rssi: src.rssi.map(|v| v.value),
            advertising_data: src.advertising_data.map(|ad| (*ad).into()),
        }
    }
}

impl From<fidl::AdvertisingData> for AdvertisingData {
    fn from(src: fidl::AdvertisingData) -> AdvertisingData {
        AdvertisingData {
            name: src.name,
            tx_power_level: src.tx_power_level.map(|v| v.value),
            appearance: src.appearance.map(|v| v.value),
            service_uuids: src.service_uuids.unwrap_or(vec![]),
            service_data: src
                .service_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            manufacturer_specific_data: src
                .manufacturer_specific_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            solicited_service_uuids: src.solicited_service_uuids.unwrap_or(vec![]),
            uris: src.uris.unwrap_or(vec![]),
        }
    }
}

impl From<fidl::AdvertisingDataDeprecated> for AdvertisingData {
    fn from(src: fidl::AdvertisingDataDeprecated) -> AdvertisingData {
        AdvertisingData {
            name: src.name,
            tx_power_level: src.tx_power_level.map(|v| v.value),
            appearance: src.appearance.map(|v| v.value),
            service_uuids: src.service_uuids.unwrap_or(vec![]),
            service_data: src
                .service_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            manufacturer_specific_data: src
                .manufacturer_specific_data
                .unwrap_or(vec![])
                .into_iter()
                .map(|data| data.into())
                .collect(),
            solicited_service_uuids: src.solicited_service_uuids.unwrap_or(vec![]),
            uris: src.uris.unwrap_or(vec![]),
        }
    }
}

impl From<fidl::ServiceDataEntry> for ServiceDataEntry {
    fn from(src: fidl::ServiceDataEntry) -> ServiceDataEntry {
        ServiceDataEntry { uuid: src.uuid, data: src.data }
    }
}

impl From<fidl::ManufacturerSpecificDataEntry> for ManufacturerSpecificDataEntry {
    fn from(src: fidl::ManufacturerSpecificDataEntry) -> ManufacturerSpecificDataEntry {
        ManufacturerSpecificDataEntry { company_id: src.company_id, data: src.data }
    }
}
