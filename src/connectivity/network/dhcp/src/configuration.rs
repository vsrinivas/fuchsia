// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use byteorder::{BigEndian, ByteOrder};
use failure::Fail;
use serde::de::{self, Visitor};
use serde_derive::{Deserialize, Serialize};
use serde_json;
use std::convert::TryFrom;
use std::io;
use std::net::Ipv4Addr;
use std::{fmt, fs};

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
    pub subnet_mask: SubnetMask,
    /// The IPv4 addresses which the server is reponsible for managing and leasing to
    /// clients.
    pub managed_addrs: Vec<Ipv4Addr>,
    /// The IPv4 addresses, in order of priority, for the default gateway/router of the local
    /// network.
    pub routers: Vec<Ipv4Addr>,
    /// The IPv4 addresses, in order of priority, for the default DNS servers of the local network.
    pub name_servers: Vec<Ipv4Addr>,
    /// Maximum allowed lease time, in case client requests a specific lease duration.
    pub max_lease_time_s: u32,
}

impl ServerConfig {
    pub fn new() -> Self {
        ServerConfig {
            server_ip: Ipv4Addr::new(0, 0, 0, 0),
            default_lease_time: 60 * 60 * 24, // One day in seconds
            subnet_mask: SubnetMask { ones: 24 },
            managed_addrs: vec![],
            routers: vec![],
            name_servers: vec![],
            max_lease_time_s: 60 * 60 * 24 * 7, // One week in seconds
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

/// Specific config values requested by the client in an option.
#[derive(Debug, PartialEq)]
pub struct RequestedConfig {
    /// Lease time requested by client in seconds.
    pub lease_time_s: Option<u32>,
}

impl RequestedConfig {
    pub fn new() -> Self {
        RequestedConfig { lease_time_s: None }
    }
}

/// Values to be provided to the client.
#[derive(Debug, PartialEq)]
pub struct ClientConfig {
    /// Lease time to be provided to the client in seconds.
    pub lease_time_s: u32,
}

impl ClientConfig {
    pub fn new(lease_time_s: u32) -> Self {
        ClientConfig { lease_time_s: lease_time_s }
    }
}

const U32_BITS: u8 = (std::mem::size_of::<u32>() * 8) as u8;

/// A bitmask which represents the boundary between the Network part and Host part of an IPv4
/// address.
#[serde(transparent)]
#[derive(Debug, Deserialize, PartialEq, Serialize)]
pub struct SubnetMask {
    #[serde(deserialize_with = "deserialize_ones")]
    /// The set high-order bits of the mask.
    ones: u8,
}

impl SubnetMask {
    /// Returns a byte-array representation of the `SubnetMask` in Network (Big-Endian) byte-order.
    pub fn octets(&self) -> [u8; 4] {
        self.to_u32().to_be_bytes()
    }

    fn to_u32(&self) -> u32 {
        let mut n: u32 = u32::max_value();
        n <<= (U32_BITS - self.ones) as u32;
        n
    }

    /// Returns the count of the set high-order bits of the `SubnetMask`.
    pub fn ones(&self) -> u8 {
        self.ones
    }

    /// Returns the Network address resulting from masking `target` with the `SubnetMask`.
    pub fn apply_to(&self, target: Ipv4Addr) -> Ipv4Addr {
        let subnet_mask_bits = self.to_u32();
        let target_bits = BigEndian::read_u32(&target.octets());
        Ipv4Addr::from(target_bits & subnet_mask_bits)
    }
}

impl TryFrom<u8> for SubnetMask {
    type Error = &'static str;

    /// Returns a `Ok(SubnetMask)` with the `ones` high-order bits set if `ones` < 32, else `Err`.
    fn try_from(ones: u8) -> Result<Self, Self::Error> {
        if ones >= U32_BITS {
            Err("failed precondition: argument must be < 32 (bit length of an IPv4 address)")
        } else {
            Ok(SubnetMask { ones })
        }
    }
}

fn deserialize_ones<'de, D>(deserializer: D) -> Result<u8, D::Error>
where
    D: de::Deserializer<'de>,
{
    struct SubnetMaskVisitor;

    impl<'de> Visitor<'de> for SubnetMaskVisitor {
        type Value = u8;

        fn expecting(&self, formatter: &mut fmt::Formatter) -> fmt::Result {
            formatter.write_str("an integer between 0 and 32")
        }

        fn visit_u64<E: de::Error>(self, v: u64) -> Result<Self::Value, E> {
            let v = std::convert::TryInto::<Self::Value>::try_into(v).map_err(
                |std::num::TryFromIntError { .. }| {
                    de::Error::invalid_value(de::Unexpected::Unsigned(v), &self)
                },
            )?;
            let v = SubnetMask::try_from(v)
                .map_err(|e| de::Error::invalid_value(de::Unexpected::Unsigned(v.into()), &e))?;
            Ok(v.ones())
        }
    }

    deserializer.deserialize_u8(SubnetMaskVisitor {})
}

#[cfg(test)]
mod tests {
    use super::{SubnetMask, TryFrom};
    use serde_json;

    #[test]
    fn subnet_mask_serializes_deserializes() -> Result<(), failure::Error> {
        let mask = SubnetMask::try_from(24).unwrap();
        let ser = serde_json::to_string(&mask)?;
        let expected = r#"24"#;
        assert_eq!(expected, ser);
        let de: SubnetMask = serde_json::from_str(&ser)?;
        assert_eq!(mask, de);
        Ok(())
    }
}
