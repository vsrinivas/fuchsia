// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Fail;
use serde_derive::{Deserialize, Serialize};
use serde_json;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fs;
use std::io;
use std::net::Ipv4Addr;

/// Attempts to load a `ServerParameters` from the json file at the provided path.
pub fn load_server_config_from_file(path: String) -> Result<ServerParameters, ConfigError> {
    let json = fs::read_to_string(path)?;
    let config = serde_json::from_str(&json)?;
    Ok(config)
}

/// A collection of the basic configuration parameters needed by the server.
#[derive(Debug, Deserialize, Serialize)]
pub struct ServerParameters {
    /// The IPv4 addresses of the host running the server.
    pub server_ips: Vec<Ipv4Addr>,
    /// The duration for which leases should be assigned to clients
    pub lease_length: LeaseLength,
    /// The IPv4 addresses which the server is responsible for managing and leasing to
    /// clients.
    pub managed_addrs: ManagedAddresses,
    /// A list of MAC addresses which are permitted to request a lease. If empty, any MAC address
    /// may request a lease.
    pub permitted_macs: Vec<fidl_fuchsia_hardware_ethernet_ext::MacAddress>,
    /// A collection of static address assignments. Any client whose MAC address has a static
    /// assignment will be offered the assigned IP address.
    pub static_assignments: HashMap<fidl_fuchsia_hardware_ethernet_ext::MacAddress, Ipv4Addr>,
}

/// The IP addresses which the server will manage and lease to clients.
#[derive(Debug, Deserialize, Serialize)]
pub struct ManagedAddresses {
    /// The network id of the subnet for which the server will manage addresses.
    pub network_id: Ipv4Addr,
    /// The broadcast id of the subnet for which the server will manage addresses.
    pub broadcast: Ipv4Addr,
    /// The subnet mask of the subnet for which the server will manage addresses.
    pub mask: SubnetMask,
    /// The inclusive starting address of the range of managed addresses.
    pub pool_range_start: Ipv4Addr,
    /// The exclusive stopping address of the range of managed addresses.
    pub pool_range_stop: Ipv4Addr,
}

impl ManagedAddresses {
    /// Returns an iterator of the `Ipv4Addr`s from `pool_range_start`, inclusive, to
    /// `pool_range_stop`, exclusive.
    pub fn pool_range(&self) -> impl Iterator<Item = Ipv4Addr> {
        let start: u32 = self.pool_range_start.into();
        let stop: u32 = self.pool_range_stop.into();
        (start..stop).map(|addr| addr.into())
    }
}

/// Parameters controlling lease duration allocation. Per,
/// https://tools.ietf.org/html/rfc2131#section-3.3, times are represented as relative times.
#[derive(Debug, Deserialize, Serialize)]
pub struct LeaseLength {
    /// The default lease duration assigned by the server.
    pub default_seconds: u32,
    /// The maximum allowable lease duration which a client can request.
    pub max_seconds: u32,
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

const U32_BITS: u8 = (std::mem::size_of::<u32>() * 8) as u8;

/// A bitmask which represents the boundary between the Network part and Host part of an IPv4
/// address.
#[derive(Clone, Copy, Debug, Deserialize, PartialEq, Serialize)]
pub struct SubnetMask {
    /// The set high-order bits of the mask.
    ones: u8,
}

impl SubnetMask {
    /// Returns a byte-array representation of the `SubnetMask` in Network (Big-Endian) byte-order.
    pub fn octets(&self) -> [u8; 4] {
        self.to_u32().to_be_bytes()
    }

    fn to_u32(&self) -> u32 {
        let n = u32::max_value();
        // overflowing_shl() will not shift arguments >= the bit length of the calling value, so we
        // must special case a mask with 0 ones.
        if self.ones == 0 {
            0
        } else {
            // overflowing_shl() must be used here for panic safety.
            let (n, _overflow) = n.overflowing_shl((U32_BITS - self.ones) as u32);
            n
        }
    }

    /// Returns the count of the set high-order bits of the `SubnetMask`.
    pub fn ones(&self) -> u8 {
        self.ones
    }

    /// Returns the Network address resulting from masking `target` with the `SubnetMask`.
    pub fn apply_to(&self, target: Ipv4Addr) -> Ipv4Addr {
        let subnet_mask_bits = self.to_u32();
        let target_bits = u32::from_be_bytes(target.octets());
        Ipv4Addr::from(target_bits & subnet_mask_bits)
    }
}

impl TryFrom<u8> for SubnetMask {
    type Error = failure::Error;

    /// Returns a `Ok(SubnetMask)` with the `ones` high-order bits set if `ones` < 32, else `Err`.
    fn try_from(ones: u8) -> Result<Self, Self::Error> {
        if ones >= U32_BITS {
            Err(failure::err_msg(
                "failed precondition: argument must be < 32 (bit length of an IPv4 address)",
            ))
        } else {
            Ok(SubnetMask { ones })
        }
    }
}

impl Into<Ipv4Addr> for SubnetMask {
    fn into(self) -> Ipv4Addr {
        Ipv4Addr::from(self.to_u32())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_into_ipv4addr_returns_ipv4addr() -> Result<(), failure::Error> {
        let v1: Ipv4Addr = SubnetMask { ones: 24 }.into();
        let v2: Ipv4Addr = SubnetMask { ones: 0 }.into();
        let v3: Ipv4Addr = SubnetMask { ones: 32 }.into();
        assert_eq!(v1, Ipv4Addr::new(255, 255, 255, 0));
        assert_eq!(v2, Ipv4Addr::new(0, 0, 0, 0));
        assert_eq!(v3, Ipv4Addr::new(255, 255, 255, 255));
        Ok(())
    }
}
