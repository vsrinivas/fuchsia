// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the network manager.

use anyhow::Error;
use thiserror::Error;

pub type Result<T> = std::result::Result<T, NetworkManager>;

/// Top-level error type the network manager.
#[derive(Error, Debug)]
pub enum NetworkManager {
    /// Errors related to LIF and LIFManager
    #[error("{}", _0)]
    LIF(Lif),
    /// Errors related to Port and PortManager
    #[error("{}", _0)]
    PORT(Port),
    /// Errors related to Services.
    #[error("{}", _0)]
    SERVICE(Service),
    /// Errors related to Config Persistence.
    #[error("{}", _0)]
    Config(Config),
    /// Errors related to HAL layer.
    #[error("{}", _0)]
    HAL(Hal),
    /// Errors related to OIR.
    #[error("{}", _0)]
    OIR(Oir),
    /// Internal errors with an attached context.
    #[error("An error occurred.")]
    INTERNAL,
    /// Config errors.
    #[error("{}", _0)]
    CONFIG(Config),
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
    fn from(_: std::io::Error) -> Self {
        NetworkManager::INTERNAL
    }
}
// TODO(bwb): fix error types
impl From<Error> for NetworkManager {
    fn from(_: anyhow::Error) -> Self {
        NetworkManager::INTERNAL
    }
}
impl From<serde_json::error::Error> for NetworkManager {
    fn from(_: serde_json::error::Error) -> Self {
        NetworkManager::INTERNAL
    }
}

/// Error type for packet LIFManager.
#[derive(Error, Debug, PartialEq)]
pub enum Lif {
    #[error("Invalid number of ports")]
    InvalidNumberOfPorts,
    #[error("Invalid port")]
    InvalidPort,
    #[error("Name in use")]
    InvalidName,
    #[error("Operation not supported for lif type")]
    TypeNotSupported,
    #[error("Vlan not supported for lif type")]
    InvalidVlan,
    #[error("LIF with same id already exists")]
    DuplicateLIF,
    #[error("LIF not found")]
    NotFound,
    #[error("Operation is not supported")]
    NotSupported,
}

/// Error type for packet PortManager.
#[derive(Error, Debug, PartialEq)]
pub enum Port {
    #[error("Port not found")]
    NotFound,
    #[error("Operation is not supported")]
    NotSupported,
}

/// Error type for Services.
#[derive(Error, Debug, PartialEq)]
pub enum Service {
    #[error("Could not enable service")]
    NotEnabled,
    #[error("Could not disable service")]
    NotDisabled,
    #[error("Failed to add new packet filter rules")]
    ErrorAddingPacketFilterRules,
    #[error("Failed to get packet filter rules")]
    ErrorGettingPacketFilterRules,
    #[error("Failed to enable IP forwarding")]
    ErrorEnableIpForwardingFailed,
    #[error("Failed to disable IP forwarding")]
    ErrorDisableIpForwardingFailed,
    #[error("Failed to update NAT rules")]
    ErrorUpdateNatFailed,
    #[error("Pending further config to update NAT rules")]
    UpdateNatPendingConfig,
    #[error("NAT is already enabled")]
    NatAlreadyEnabled,
    #[error("NAT is not enabled")]
    NatNotEnabled,
    #[error("Service is not supported")]
    NotSupported,
}

/// Error type for packet HAL.
#[derive(Error, Debug, PartialEq)]
pub enum Hal {
    #[error("Could not create bridge")]
    BridgeNotCreated,
    #[error("Could not find bridge")]
    BridgeNotFound,
    #[error("Operation failed")]
    OperationFailed,
}

/// Error type for config persistence.
#[derive(Error, Debug, PartialEq)]
pub enum Config {
    #[error("No config has been loaded yet")]
    NoConfigLoaded,
    #[error("Device config paths have not been set up yet")]
    ConfigPathsNotSet,
    #[error("The requested config file was not found: {}", path)]
    ConfigNotFound { path: String },
    #[error("Could not load the requested config: {}, because: {}", path, error)]
    ConfigNotLoaded { path: String, error: String },
    #[error("Could not load the device schema: {}", path)]
    SchemaNotLoaded { path: String },
    #[error("Failed to deserialize config: {}, because: {}", path, error)]
    FailedToDeserializeConfig { path: String, error: String },
    #[error("Failed to load device schema: {}, because {}", path, error)]
    FailedToLoadDeviceSchema { path: String, error: String },
    #[error("Failed to validate device config: {}, because: {}", path, error)]
    FailedToValidateConfig { path: String, error: String },
    #[error("The requested config section does not exist: {}", msg)]
    NotFound { msg: String },
    #[error("Config is malformed: {}", msg)]
    Malformed { msg: String },
    #[error("Operation not supported: {}", msg)]
    NotSupported { msg: String },
}

/// Error type for OIR.
#[derive(Error, Debug, PartialEq)]
pub enum Oir {
    #[error("Unsupported port type")]
    UnsupportedPort,
    #[error("Information missing")]
    MissingInformation,
    #[error("Invalid device path")]
    InvalidPath,
    #[error("Inconsistent state")]
    InconsistentState,
    #[error("Operation failed")]
    OperationFailed,
}
