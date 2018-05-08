// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_bluetooth;
use fidl_bluetooth_control::{AdapterInfo, AdapterState};

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
    () => (fidl_bluetooth::Status { error: None } );

    ($error_code:ident) => (fidl_bluetooth::Status { error: Some(Box::new(
                    fidl_bluetooth::Error {
                        description: None,
                        protocol_error_code: 0,
                        error_code: fidl_bluetooth::ErrorCode::$error_code,
                    }))
    });

    ($error_code:ident, $description:expr) => (fidl_bluetooth::Status { error: Some(Box::new(
                    fidl_bluetooth::Error {
                        description: Some($description.to_string()),
                        protocol_error_code: 0,
                        error_code: fidl_bluetooth::ErrorCode::$error_code,
                    }))
    });
}

/// Clone Adapter Info
/// Only here until Rust bindings can derive `Clone`
pub fn clone_adapter_info(a: &AdapterInfo) -> AdapterInfo {
    let state = match a.state {
        Some(ref s) => Some(Box::new(clone_adapter_state(&**s))),
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
/// Only here until Rust bindings can derive `Clone`
pub fn clone_bt_fidl_bool(a: &fidl_bluetooth::Bool) -> fidl_bluetooth::Bool {
    fidl_bluetooth::Bool { value: a.value }
}

/// Clone Adapter State
/// Only here until Rust bindings can derive `Clone`
pub fn clone_adapter_state(a: &AdapterState) -> AdapterState {
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
