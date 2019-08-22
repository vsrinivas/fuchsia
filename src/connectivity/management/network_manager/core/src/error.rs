// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the network manager.

use failure::Fail;

pub type Result<T> = std::result::Result<T, NetworkManager>;

/// Top-level error type the network manager.
#[derive(Fail, Debug)]
pub enum NetworkManager {
    /// Errors related to LIF and LIFManager
    #[fail(display = "{}", _0)]
    LIF(#[cause] Lif),
    /// Errors related to Port and PortManager
    #[fail(display = "{}", _0)]
    PORT(#[cause] Port),
    /// Errors related to Services.
    #[fail(display = "{}", _0)]
    SERVICE(#[cause] Service),
    /// Errors related to HAL layer.
    #[fail(display = "{}", _0)]
    HAL(#[cause] Hal),
    // Add error types here.
}

impl From<Lif> for NetworkManager {
    fn from(e: Lif) -> Self {
        NetworkManager::LIF(e)
    }
}
impl From<Port> for NetworkManager {
    fn from(e: Port) -> Self {
        NetworkManager::PORT(e)
    }
}
impl From<Hal> for NetworkManager {
    fn from(e: Hal) -> Self {
        NetworkManager::HAL(e)
    }
}

/// Error type for packet LIFManager.
#[derive(Fail, Debug, PartialEq)]
pub enum Lif {
    #[fail(display = "Invalid number of ports")]
    InvalidNumberOfPorts,
    #[fail(display = "Invalid port")]
    InvalidPort,
    #[fail(display = "Name in use")]
    InvalidName,
    #[fail(display = "Operation not supported for lif type")]
    TypeNotSupported,
    #[fail(display = "Vlan not supported for lif type")]
    InvalidVlan,
    #[fail(display = "LIF with same id already exists")]
    DuplicateLIF,
    #[fail(display = "LIF not found")]
    NotFound,
    #[fail(display = "Operation is not supported")]
    NotSupported,
}

/// Error type for packet PortManager.
#[derive(Fail, Debug, PartialEq)]
pub enum Port {
    #[fail(display = "Port not found")]
    NotFound,
    #[fail(display = "Operation is not supported")]
    NotSupported,
}

/// Error type for Services.
#[derive(Fail, Debug, PartialEq)]
pub enum Service {
    #[fail(display = "Could not enable service")]
    NotEnabled,
    #[fail(display = "Could not disable service")]
    NotDisabled,
    #[fail(display = "Service is not supported")]
    NotSupported,
}
/// Error type for packet HAL.
#[derive(Fail, Debug, PartialEq)]
pub enum Hal {
    #[fail(display = "Could not create bridge")]
    BridgeNotCreated,
    #[fail(display = "Could not find bridge")]
    BridgeNotFound,
    #[fail(display = "Operation failed")]
    OperationFailed,
}
