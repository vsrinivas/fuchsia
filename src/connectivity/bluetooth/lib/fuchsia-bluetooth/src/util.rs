// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth::{self, Int8},
    fidl_fuchsia_bluetooth_control::{AdapterInfo, AdapterState, RemoteDevice},
    fidl_fuchsia_bluetooth_host::{
        BondingData, BredrData, Key, LeConnectionParameters, LeData, Ltk, SecurityProperties,
    },
};

/// Macro to help make cloning host dispatchers nicer
#[macro_export]
macro_rules! make_clones {
    ($obj:ident => $($copy:ident),* $(,)*) => {
        $( let $copy = $obj.clone(); )*
    }
}

/// Macro to help build bluetooth fidl statuses.
/// No Args is a success
/// One Arg is the error type
/// Two Args is the error type & a description
#[macro_export]
macro_rules! bt_fidl_status {
    () => {
        fidl_fuchsia_bluetooth::Status { error: None }
    };

    ($error_code:ident) => {
        fidl_fuchsia_bluetooth::Status {
            error: Some(Box::new(fidl_fuchsia_bluetooth::Error {
                description: None,
                protocol_error_code: 0,
                error_code: fidl_fuchsia_bluetooth::ErrorCode::$error_code,
            })),
        }
    };

    ($error_code:ident, $description:expr) => {
        fidl_fuchsia_bluetooth::Status {
            error: Some(Box::new(fidl_fuchsia_bluetooth::Error {
                description: Some($description.to_string()),
                protocol_error_code: 0,
                error_code: fidl_fuchsia_bluetooth::ErrorCode::$error_code,
            })),
        }
    };
}

// The following functions allow FIDL types to be cloned. These are currently necessary as the
// auto-generated binding types do not derive `Clone`.

/// Clone Adapter Info
pub fn clone_host_info(a: &AdapterInfo) -> AdapterInfo {
    let state = match a.state {
        Some(ref s) => Some(Box::new(clone_host_state(&**s))),
        None => None,
    };
    AdapterInfo {
        identifier: a.identifier.clone(),
        technology: a.technology.clone(),
        address: a.address.clone(),
        state: state,
    }
}

/// Clone Bluetooth Fidl bool type
pub fn clone_bt_fidl_bool(a: &fidl_fuchsia_bluetooth::Bool) -> fidl_fuchsia_bluetooth::Bool {
    fidl_fuchsia_bluetooth::Bool { value: a.value }
}

/// Clone Adapter State
pub fn clone_host_state(a: &AdapterState) -> AdapterState {
    let discoverable = match a.discoverable {
        Some(ref disc) => Some(Box::new(clone_bt_fidl_bool(disc))),
        None => None,
    };

    let discovering = match a.discovering {
        Some(ref disc) => Some(Box::new(clone_bt_fidl_bool(disc))),
        None => None,
    };

    AdapterState {
        local_name: a.local_name.clone(),
        discovering: discovering,
        discoverable: discoverable,
        local_service_uuids: a.local_service_uuids.clone(),
    }
}

/// Clone BondingData
pub fn clone_bonding_data(bd: &BondingData) -> BondingData {
    BondingData {
        identifier: bd.identifier.clone(),
        local_address: bd.local_address.clone(),
        name: bd.name.clone(),
        le: bd.le.as_ref().map(|v| Box::new(clone_le_data(&v))),
        bredr: bd.bredr.as_ref().map(|v| Box::new(clone_bredr_data(&v))),
    }
}

fn clone_le_conn_params(cp: &LeConnectionParameters) -> LeConnectionParameters {
    LeConnectionParameters { ..*cp }
}

fn clone_security_props(sp: &SecurityProperties) -> SecurityProperties {
    SecurityProperties { ..*sp }
}

fn clone_key(key: &Key) -> Key {
    Key { security_properties: clone_security_props(&key.security_properties), ..*key }
}

fn clone_ltk(ltk: &Ltk) -> Ltk {
    Ltk { key: clone_key(&ltk.key), ..*ltk }
}

fn clone_le_data(le: &LeData) -> LeData {
    LeData {
        address: le.address.clone(),
        connection_parameters: le
            .connection_parameters
            .as_ref()
            .map(|v| Box::new(clone_le_conn_params(&v))),
        services: le.services.clone(),
        ltk: le.ltk.as_ref().map(|v| Box::new(clone_ltk(&v))),
        irk: le.irk.as_ref().map(|v| Box::new(clone_key(&v))),
        csrk: le.csrk.as_ref().map(|v| Box::new(clone_key(&v))),
        ..*le
    }
}

fn clone_bredr_data(bredr: &BredrData) -> BredrData {
    BredrData {
        address: bredr.address.clone(),
        services: bredr.services.clone(),
        link_key: bredr.link_key.as_ref().map(|v| Box::new(clone_ltk(&v))),
        ..*bredr
    }
}

/// Clone RemoteDevice data, as clone is not implemented for FIDL types
pub fn clone_remote_device(d: &RemoteDevice) -> RemoteDevice {
    fn copy_option_int8(opt: &Option<Box<Int8>>) -> Option<Box<Int8>> {
        match opt {
            Some(i) => Some(Box::new(Int8 { value: i.value })),
            None => None,
        }
    }
    RemoteDevice {
        identifier: d.identifier.clone(),
        address: d.address.clone(),
        technology: d.technology.clone(),
        name: d.name.clone(),
        appearance: d.appearance.clone(),
        rssi: copy_option_int8(&d.rssi),
        tx_power: copy_option_int8(&d.tx_power),
        connected: d.connected,
        bonded: d.bonded,
        service_uuids: d.service_uuids.iter().cloned().collect(),
    }
}
