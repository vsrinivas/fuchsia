// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error};
use fidl_fuchsia_bluetooth_control::RemoteDevice;
use fidl_fuchsia_bluetooth_gatt::{
    AttributePermissions, Characteristic, Descriptor, SecurityRequirements, ServiceInfo,
};
use serde_derive::{Deserialize, Serialize};

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
        BleScanResponse { id, name, connectable }
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

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct SecurityRequirementsContainer {
    pub encryption_required: bool,
    pub authentication_required: bool,
    pub authorization_required: bool,
}

impl SecurityRequirementsContainer {
    pub fn new(info: Option<Box<SecurityRequirements>>) -> SecurityRequirementsContainer {
        match info {
            Some(s) => {
                let sec = *s;
                SecurityRequirementsContainer {
                    encryption_required: sec.encryption_required,
                    authentication_required: sec.authentication_required,
                    authorization_required: sec.authorization_required,
                }
            }
            None => SecurityRequirementsContainer {
                encryption_required: false,
                authentication_required: false,
                authorization_required: false,
            },
        }
    }
}

#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct AttributePermissionsContainer {
    pub read: SecurityRequirementsContainer,
    pub write: SecurityRequirementsContainer,
    pub update: SecurityRequirementsContainer,
}

impl AttributePermissionsContainer {
    pub fn new(
        info: Option<Box<AttributePermissions>>,
    ) -> Result<AttributePermissionsContainer, Error> {
        match info {
            Some(p) => {
                let perm = *p;
                Ok(AttributePermissionsContainer {
                    read: SecurityRequirementsContainer::new(perm.read),
                    write: SecurityRequirementsContainer::new(perm.write),
                    update: SecurityRequirementsContainer::new(perm.update),
                })
            }
            None => bail!("Unable to get information of AttributePermissions."),
        }
    }
}

// Discover Characteristic response to hold characteristic info
// as Characteristics are not serializable.
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GattcDiscoverDescriptorResponse {
    pub id: u64,
    pub permissions: Option<AttributePermissionsContainer>,
    pub uuid_type: String,
}

impl GattcDiscoverDescriptorResponse {
    pub fn new(info: Vec<Descriptor>) -> Vec<GattcDiscoverDescriptorResponse> {
        let mut res = Vec::new();
        for v in info {
            let copy = GattcDiscoverDescriptorResponse {
                id: v.id,
                permissions: match AttributePermissionsContainer::new(v.permissions) {
                    Ok(n) => Some(n),
                    Err(_) => None,
                },
                uuid_type: v.type_,
            };
            res.push(copy)
        }
        res
    }
}

// Discover Characteristic response to hold characteristic info
// as Characteristics are not serializable.
#[derive(Serialize, Deserialize, Clone, Debug)]
pub struct GattcDiscoverCharacteristicResponse {
    pub id: u64,
    pub properties: u32,
    pub permissions: Option<AttributePermissionsContainer>,
    pub uuid_type: String,
    pub descriptors: Vec<GattcDiscoverDescriptorResponse>,
}

impl GattcDiscoverCharacteristicResponse {
    pub fn new(info: Vec<Characteristic>) -> Vec<GattcDiscoverCharacteristicResponse> {
        let mut res = Vec::new();
        for v in info {
            let copy = GattcDiscoverCharacteristicResponse {
                id: v.id,
                properties: v.properties,
                permissions: match AttributePermissionsContainer::new(v.permissions) {
                    Ok(n) => Some(n),
                    Err(_) => None,
                },
                uuid_type: v.type_,
                descriptors: {
                    match v.descriptors {
                        Some(d) => GattcDiscoverDescriptorResponse::new(d),
                        None => Vec::new(),
                    }
                },
            };
            res.push(copy)
        }
        res
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
            let copy =
                BleConnectPeripheralResponse { id: v.id, primary: v.primary, uuid_type: v.type_ };
            res.push(copy)
        }
        res
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct CustomRemoteDevice {
    pub address: String,
    pub id: String,
    pub name: Option<String>,
    pub connected: bool,
    pub bonded: bool,
    pub service_uuids: Vec<String>,
}

impl From<&RemoteDevice> for CustomRemoteDevice {
    fn from(device: &RemoteDevice) -> Self {
        CustomRemoteDevice {
            address: device.address.clone(),
            id: device.identifier.clone(),
            name: device.name.clone(),
            connected: device.connected,
            bonded: device.bonded,
            service_uuids: device.service_uuids.clone(),
        }
    }
}
