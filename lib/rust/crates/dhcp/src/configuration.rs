// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use serde_json;
use std::fmt::{self, Display, Formatter};
use std::fs;
use std::io;
use std::net::Ipv4Addr;

/// Attempts to load a `ServerConfig` from the json file at the provided path.
pub fn load_server_config_from_file(path: String) -> Result<ServerConfig, ConfigError> {
    let json = fs::read_to_string(path)?;
    let config = serde_json::from_str(&json)?;
    Ok(config)
}

/// A collection of the basic configuration parameters needed by the server.
#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct ServerConfig {
    /// The IPv4 address of the host running the server.
    pub server_ip: Ipv4Addr,
    /// The default time (in seconds) assigned to IP address leases assigned by the server.
    // TODO(atait): change field type to zx::Duration
    pub default_lease_time: u32,
    /// The number of bits to mask the subnet address from the host address in an IPv4Addr.
    pub subnet_mask: u8,
    /// The IPv4 addresses which the server is reponsible for managing and leasing to
    /// clients.
    pub managed_addrs: Vec<Ipv4Addr>,
    /// The IPv4 addresses, in order of priority, for the default gateway/router of the local
    /// network.
    pub routers: Vec<Ipv4Addr>,
    /// The IPv4 addresses, in order of priority, for the default DNS servers of the local network.
    pub name_servers: Vec<Ipv4Addr>,
}

impl ServerConfig {
    pub fn new() -> Self {
        ServerConfig {
            server_ip: Ipv4Addr::new(0, 0, 0, 0),
            default_lease_time: 60 * 60 * 24, // One day in seconds
            subnet_mask: 24,
            managed_addrs: vec![],
            routers: vec![],
            name_servers: vec![],
        }
    }
}

/// A wrapper around the error types which can be returned when loading a
/// `ServerConfig` from file with `load_server_config_from_file()`.
#[derive(Debug, Fail)]
pub enum ConfigError {
    #[fail(display = "io error: {}", _0)]
    IoError(io::Error),
    #[fail(display = "json deserialization error: {}", _0)]
    JsonError(serde_json::Error),
}

impl From<io::Error> for ConfigError {
    fn from(e: io::Error) -> Self {
        ConfigError::IoError(e)
    }
}

impl From<serde_json::Error> for ConfigError {
    fn from(e: serde_json::Error) -> Self {
        ConfigError::JsonError(e)
    }
}
