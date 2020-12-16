// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Custom error types for the network manager.

use std::path::PathBuf;
use thiserror::Error;

pub type Result<T> = std::result::Result<T, NetworkManager>;

/// Top-level error type the network manager.
#[derive(Error, Debug)]
pub enum NetworkManager {
    /// Errors related to LIF and LIFManager
    #[error("{0}")]
    Lif(#[from] Lif),
    /// Errors related to Port and PortManager
    #[error("{0}")]
    Port(#[from] Port),
    /// Errors related to Services.
    #[error("{0}")]
    Service(#[from] Service),
    /// Errors related to Configuration.
    #[error("{0}")]
    Config(#[from] Config),
    /// Errors related to HAL layer.
    #[error("{0}")]
    Hal(#[from] Hal),
    /// Errors related to OIR.
    #[error("{0}")]
    Oir(#[from] Oir),
    /// Internal errors with an attached context.
    #[error("An error occurred.")]
    Internal,
    // Add error types here.
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

/// Error type for fuchsia.net.filter FIDL.
#[derive(Error, Debug)]
pub enum PacketFilterFidl {
    #[error("Non-Ok status: {0:?}")]
    Status(fidl_fuchsia_net_filter::Status),
    #[error("FIDL error: {0}")]
    Fidl(fidl::Error),
}

/// Error type for packet filter rule parsing.
#[derive(Error, Debug, PartialEq)]
pub enum PacketFilterParse {
    #[error("CidrAddress missing address field")]
    MissingAddress,
    #[error("CidrAddress missing prefix_length field")]
    MissingPrefixLength,
    #[error("More than one port range specified: {0:?}")]
    TooManyPortRanges(Vec<fidl_fuchsia_router_config::PortRange>),
}

/// Error type for Services.
#[derive(Error, Debug)]
pub enum Service {
    #[error("Could not enable service")]
    NotEnabled,
    #[error("Could not disable service")]
    NotDisabled,
    #[error("Could not connect to fuchsia.net.filter/Filter service")]
    PacketFilterServiceConnect,
    #[error("Failed to add new packet filter rules")]
    ErrorAddingPacketFilterRules,
    #[error("Failed to clear packet filter rules")]
    ErrorClearingPacketFilterRules,
    #[error("Failed to get packet filter rules: {0}")]
    ErrorGettingPacketFilterRules(PacketFilterFidl),
    #[error("Failed to parse packet filter rule")]
    ErrorParsingPacketFilterRule(#[from] PacketFilterParse),
    #[error("Failed to update packet filter rule: {0}")]
    ErrorUpdatingPacketFilterRules(PacketFilterFidl),
    #[error("Failed to enable IP forwarding")]
    ErrorEnableIpForwardingFailed,
    #[error("Failed to disable IP forwarding")]
    ErrorDisableIpForwardingFailed,
    #[error("Failed to update NAT rules")]
    ErrorUpdateNatFailed,
    #[error("Pending further config to update NAT rules")]
    UpdateNatPendingConfig,
    #[error("NAT is not enabled")]
    NatNotEnabled,
    #[error("Error while configuring NAT: {msg}")]
    NatConfigError { msg: String },
    #[error("Service is not supported")]
    NotSupported,
}

/// Error type for packet HAL.
#[derive(Error, Debug)]
pub enum Hal {
    #[error("Could not create bridge")]
    BridgeNotCreated,
    #[error("Could not find bridge")]
    BridgeNotFound,
    #[error("FIDL error: {context}")]
    Fidl { context: String, source: fidl::Error },
    #[error("Operation failed")]
    OperationFailed,
}

/// Error type for config persistence.
#[derive(Error, Debug)]
pub enum Config {
    #[error("No config has been loaded yet")]
    NoConfigLoaded,
    #[error("Device config paths have not been set up yet")]
    ConfigPathsNotSet,
    #[error("The requested config file was not found: {path}")]
    ConfigNotFound { path: PathBuf },
    #[error("Could not load the requested config: {path}")]
    ConfigNotLoaded { path: PathBuf, source: std::io::Error },
    #[error("Failed to deserialize config: {path}")]
    FailedToDeserializeConfig { path: PathBuf, source: serde_json::Error },
    #[error("Failed to validate device config: {path}, because: {error}")]
    FailedToValidateConfig { path: String, error: String },
    #[error("The requested config section does not exist: {msg}")]
    NotFound { msg: String },
    #[error("Config is malformed: {msg}")]
    Malformed { msg: String },
    #[error("Operation not supported: {msg}")]
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
