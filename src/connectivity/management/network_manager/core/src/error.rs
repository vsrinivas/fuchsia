// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the network manager.

use core::fmt::{self, Display};
use failure::{Context, Fail};

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
    /// Errors related to Config Persistence.
    #[fail(display = "{}", _0)]
    Config(#[cause] Config),
    /// Errors related to HAL layer.
    #[fail(display = "{}", _0)]
    HAL(#[cause] Hal),
    /// Errors related to OIR.
    #[fail(display = "{}", _0)]
    OIR(#[cause] Oir),
    /// Internal errors with an attached context.
    #[fail(display = "An error occurred.")]
    INTERNAL(ErrorWithContext),
    /// Config errors.
    #[fail(display = "{}", _0)]
    CONFIG(#[cause] Config),
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
impl From<Oir> for NetworkManager {
    fn from(e: Oir) -> Self {
        NetworkManager::OIR(e)
    }
}
impl From<Service> for NetworkManager {
    fn from(e: Service) -> Self {
        NetworkManager::SERVICE(e)
    }
}
impl From<std::io::Error> for NetworkManager {
    fn from(e: std::io::Error) -> Self {
        NetworkManager::INTERNAL(ErrorWithContext { inner: Context::new(e.to_string()) })
    }
}
impl From<serde_json::error::Error> for NetworkManager {
    fn from(e: serde_json::error::Error) -> Self {
        NetworkManager::INTERNAL(ErrorWithContext { inner: Context::new(e.to_string()) })
    }
}
// Allows adding more context via a &str
impl From<Context<&'static str>> for NetworkManager {
    fn from(inner: Context<&'static str>) -> Self {
        NetworkManager::INTERNAL(ErrorWithContext { inner: Context::new(inner.to_string()) })
    }
}

#[derive(Fail, Debug)]
pub struct ErrorWithContext {
    inner: Context<String>,
}
impl Display for ErrorWithContext {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, f)
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
    #[fail(display = "Failed to add new packet filter rules")]
    ErrorAddingPacketFilterRules,
    #[fail(display = "Failed to get packet filter rules")]
    ErrorGettingPacketFilterRules,
    #[fail(display = "Failed to enable IP forwarding")]
    ErrorEnableIpForwardingFailed,
    #[fail(display = "Failed to disable IP forwarding")]
    ErrorDisableIpForwardingFailed,
    #[fail(display = "Failed to update NAT rules")]
    ErrorUpdateNatFailed,
    #[fail(display = "Pending further config to update NAT rules")]
    UpdateNatPendingConfig,
    #[fail(display = "NAT is already enabled")]
    NatAlreadyEnabled,
    #[fail(display = "NAT is not enabled")]
    NatNotEnabled,
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

/// Error type for config persistence.
#[derive(Fail, Debug, PartialEq)]
pub enum Config {
    #[fail(display = "Device config paths have not been set up yet")]
    ConfigPathsNotSet,
    #[fail(display = "The requested config file was not found: {}", path)]
    ConfigNotFound { path: String },
    #[fail(display = "Could not load the requested config: {}, because: {}", path, error)]
    ConfigNotLoaded { path: String, error: String },
    #[fail(display = "Could not load the device schema: {}", path)]
    SchemaNotLoaded { path: String },
    #[fail(display = "Failed to deserialize config: {}, because: {}", path, error)]
    FailedToDeserializeConfig { path: String, error: String },
    #[fail(display = "Failed to load device schema: {}, because {}", path, error)]
    FailedToLoadDeviceSchema { path: String, error: String },
    #[fail(display = "Failed to validate device config: {}, because: {}", path, error)]
    FailedToValidateConfig { path: String, error: String },
}

/// Error type for OIR.
#[derive(Fail, Debug, PartialEq)]
pub enum Oir {
    #[fail(display = "Unsupported port type")]
    UnsupportedPort,
    #[fail(display = "Information missing")]
    MissingInformation,
    #[fail(display = "Invalid device path")]
    InvalidPath,
    #[fail(display = "Inconsistent state")]
    InconsistentState,
    #[fail(display = "Operation failed")]
    OperationFailed,
}
