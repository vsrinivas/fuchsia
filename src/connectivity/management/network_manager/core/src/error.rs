// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the network manager.

use anyhow::Error;
use thiserror::Error;

pub type Result<T> = std::result::Result<T, NetworkManager>;

/// Top-level error type the network manager.
// TODO(45692): CamelCase these enums.
#[derive(Error, Debug, PartialEq)]
pub enum NetworkManager {
    /// Errors related to LIF and LIFManager
    #[error("{}", _0)]
    Lif(Lif),
    /// Errors related to Port and PortManager
    #[error("{}", _0)]
    Port(Port),
    /// Errors related to Services.
    #[error("{}", _0)]
    Service(Service),
    /// Errors related to Configuration.
    #[error("{}", _0)]
    Config(Config),
    /// Errors related to HAL layer.
    #[error("{}", _0)]
    Hal(Hal),
    /// Errors related to OIR.
    #[error("{}", _0)]
    Oir(Oir),
    /// Internal errors with an attached context.
    #[error("An error occurred.")]
    Internal,
    // Add error types here.
}

impl From<Config> for NetworkManager {
    fn from(e: Config) -> Self {
        NetworkManager::Config(e)
    }
}
impl From<Hal> for NetworkManager {
    fn from(e: Hal) -> Self {
        NetworkManager::Hal(e)
    }
}
impl From<Lif> for NetworkManager {
    fn from(e: Lif) -> Self {
        NetworkManager::Lif(e)
    }
}
impl From<Oir> for NetworkManager {
    fn from(e: Oir) -> Self {
        NetworkManager::Oir(e)
    }
}
impl From<Port> for NetworkManager {
    fn from(e: Port) -> Self {
        NetworkManager::Port(e)
    }
}
impl From<Service> for NetworkManager {
    fn from(e: Service) -> Self {
        NetworkManager::Service(e)
    }
}
impl From<std::io::Error> for NetworkManager {
    fn from(_: std::io::Error) -> Self {
        NetworkManager::Internal
    }
}
// TODO(bwb): fix error types
impl From<Error> for NetworkManager {
    fn from(_: anyhow::Error) -> Self {
        NetworkManager::Internal
    }
}
impl From<serde_json::error::Error> for NetworkManager {
    fn from(_: serde_json::error::Error) -> Self {
        NetworkManager::Internal
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
    #[error("Vlan is not valid ")]
    InvalidVlan,
    #[error("LIF with same id already exists")]
    DuplicateLIF,
    #[error("LIF not found")]
    NotFound,
    #[error("Invalid Parameter")]
    InvalidParameter,
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
    #[error("Failed to clear packet filter rules")]
    ErrorClearingPacketFilterRules,
    #[error("Failed to get packet filter rules")]
    ErrorGettingPacketFilterRules,
    #[error("Error while parsing packet filter rule: {}", msg)]
    ErrorParsingPacketFilterRule { msg: String },
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
    #[error("Error while configuring NAT: {}", msg)]
    NatConfigError { msg: String },
    #[error("Service is not supported")]
    NotSupported,
    #[error("FIDL service error: {}", msg)]
    FidlError { msg: String },
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
    #[error("Failed to get ACL entries")]
    FailedToGetAclEntries,
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
