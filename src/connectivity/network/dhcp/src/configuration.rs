// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::protocol::{FidlCompatible, FromFidlExt, IntoFidlExt};
use anyhow::Context;
use serde_derive::{Deserialize, Serialize};
use serde_json;
use std::collections::HashMap;
use std::convert::TryFrom;
use std::fs;
use std::io;
use std::net::Ipv4Addr;
use thiserror::Error;

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
    pub permitted_macs: PermittedMacs,
    /// A collection of static address assignments. Any client whose MAC address has a static
    /// assignment will be offered the assigned IP address.
    pub static_assignments: StaticAssignments,
    /// Enables server behavior where the server ARPs an IP address prior to issuing
    /// it in a lease.
    pub arp_probe: bool,
}

/// Parameters controlling lease duration allocation. Per,
/// https://tools.ietf.org/html/rfc2131#section-3.3, times are represented as relative times.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct LeaseLength {
    /// The default lease duration assigned by the server.
    pub default_seconds: u32,
    /// The maximum allowable lease duration which a client can request.
    pub max_seconds: u32,
}

impl FidlCompatible<fidl_fuchsia_net_dhcp::LeaseLength> for LeaseLength {
    type FromError = anyhow::Error;
    type IntoError = never::Never;

    fn try_from_fidl(fidl: fidl_fuchsia_net_dhcp::LeaseLength) -> Result<Self, Self::FromError> {
        if let fidl_fuchsia_net_dhcp::LeaseLength { default: Some(default_seconds), max } = fidl {
            Ok(LeaseLength {
                default_seconds,
                // Per fuchsia.net.dhcp, if omitted, max defaults to the value of default.
                max_seconds: max.unwrap_or(default_seconds),
            })
        } else {
            Err(anyhow::format_err!(
                "fuchsia.net.dhcp.LeaseLength missing required field: {:?}",
                fidl
            ))
        }
    }

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net_dhcp::LeaseLength, Self::IntoError> {
        let LeaseLength { default_seconds, max_seconds } = self;
        Ok(fidl_fuchsia_net_dhcp::LeaseLength {
            default: Some(default_seconds),
            max: Some(max_seconds),
        })
    }
}

/// The IP addresses which the server will manage and lease to clients.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
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

impl FidlCompatible<fidl_fuchsia_net_dhcp::AddressPool> for ManagedAddresses {
    type FromError = anyhow::Error;
    type IntoError = never::Never;

    fn try_from_fidl(fidl: fidl_fuchsia_net_dhcp::AddressPool) -> Result<Self, Self::FromError> {
        if let fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(network_id),
            broadcast: Some(broadcast),
            mask: Some(mask),
            pool_range_start: Some(pool_range_start),
            pool_range_stop: Some(pool_range_stop),
        } = fidl
        {
            let mask = SubnetMask::try_from_fidl(mask)
                .context("fuchsia.net.dhcp.AddressPool contained invalid mask")?;
            let network_id = Ipv4Addr::from_fidl(network_id);
            let broadcast = Ipv4Addr::from_fidl(broadcast);
            let pool_range_start = Ipv4Addr::from_fidl(pool_range_start);
            let pool_range_stop = Ipv4Addr::from_fidl(pool_range_stop);
            if mask.apply_to(network_id) != network_id {
                Err(anyhow::format_err!(
                    "fuchsia.net.dhcp.AddressPool contained wrong mask (/{}) for network_id ({})",
                    mask.ones(),
                    network_id
                ))
            } else if mask.apply_to(broadcast) != network_id {
                Err(anyhow::format_err!("fuchsia.net.dhcp.AddressPool contained broadcast ({}) outside of network_id ({})", broadcast, network_id))
            } else if mask.apply_to(pool_range_start) != network_id {
                Err(anyhow::format_err!("fuchsia.net.dhcp.AddressPool contained pool_range_start ({}) outside of network_id ({})", pool_range_start, network_id))
            } else if mask.apply_to(pool_range_stop) != network_id {
                Err(anyhow::format_err!("fuchsia.net.dhcp.AddressPool contained pool_range_stop ({}) outside of network_id ({})", pool_range_stop, network_id))
            } else if pool_range_start > pool_range_stop {
                Err(anyhow::format_err!("fuchsia.net.dhpc.AddressPool contained pool_range_start ({}) > pool_range_stop ({})", pool_range_start, pool_range_stop))
            } else {
                Ok(Self { network_id, broadcast, mask, pool_range_start, pool_range_stop })
            }
        } else {
            Err(anyhow::format_err!("fuchsia.net.dhcp.AddressPool missing fields: {:?}", fidl))
        }
    }

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net_dhcp::AddressPool, Self::IntoError> {
        let ManagedAddresses { network_id, broadcast, mask, pool_range_start, pool_range_stop } =
            self;
        Ok(fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(network_id.into_fidl()),
            broadcast: Some(broadcast.into_fidl()),
            mask: Some(mask.into_fidl()),
            pool_range_start: Some(pool_range_start.into_fidl()),
            pool_range_stop: Some(pool_range_stop.into_fidl()),
        })
    }
}

/// A list of MAC addresses which are permitted to request a lease.
#[derive(Clone, Debug, Serialize, Deserialize)]
pub struct PermittedMacs(pub Vec<fidl_fuchsia_net_ext::MacAddress>);

impl FidlCompatible<Vec<fidl_fuchsia_net::MacAddress>> for PermittedMacs {
    type FromError = never::Never;
    type IntoError = never::Never;

    fn try_from_fidl(fidl: Vec<fidl_fuchsia_net::MacAddress>) -> Result<Self, Self::FromError> {
        Ok(PermittedMacs(fidl.into_iter().map(|mac| mac.into()).collect()))
    }

    fn try_into_fidl(self) -> Result<Vec<fidl_fuchsia_net::MacAddress>, Self::IntoError> {
        Ok(self.0.into_iter().map(|mac| mac.into()).collect())
    }
}

/// A collection of static address assignments. Any client whose MAC address has a static
/// assignment will be offered the assigned IP address.
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub struct StaticAssignments(pub HashMap<fidl_fuchsia_net_ext::MacAddress, Ipv4Addr>);

impl FidlCompatible<Vec<fidl_fuchsia_net_dhcp::StaticAssignment>> for StaticAssignments {
    type FromError = anyhow::Error;
    type IntoError = never::Never;

    fn try_from_fidl(
        fidl: Vec<fidl_fuchsia_net_dhcp::StaticAssignment>,
    ) -> Result<Self, Self::FromError> {
        match fidl.into_iter().try_fold(HashMap::new(), |mut acc, assignment| {
            if let (Some(host), Some(assigned_addr)) = (assignment.host, assignment.assigned_addr) {
                let mac = fidl_fuchsia_net_ext::MacAddress::from(host);
                match acc.insert(mac, Ipv4Addr::from_fidl(assigned_addr)) {
                    Some(_ip) => Err(anyhow::format_err!(
                        "fuchsia.net.dhcp.StaticAssignment contained multiple entries for {}",
                        mac
                    )),
                    None => Ok(acc),
                }
            } else {
                Err(anyhow::format_err!(
                    "fuchsia.net.dhcp.StaticAssignment contained entry with missing fields: {:?}",
                    assignment
                ))
            }
        }) {
            Ok(static_assignments) => Ok(StaticAssignments(static_assignments)),
            Err(e) => Err(e),
        }
    }

    fn try_into_fidl(
        self,
    ) -> Result<Vec<fidl_fuchsia_net_dhcp::StaticAssignment>, Self::IntoError> {
        Ok(self
            .0
            .into_iter()
            .map(|(host, assigned_addr)| fidl_fuchsia_net_dhcp::StaticAssignment {
                host: Some(host.into()),
                assigned_addr: Some(assigned_addr.into_fidl()),
            })
            .collect())
    }
}

/// A wrapper around the error types which can be returned when loading a
/// `ServerConfig` from file with `load_server_config_from_file()`.
#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("io error: {}", _0)]
    IoError(io::Error),
    #[error("json deserialization error: {}", _0)]
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
    type Error = anyhow::Error;

    /// Returns a `Ok(SubnetMask)` with the `ones` high-order bits set if `ones` < 32, else `Err`.
    fn try_from(ones: u8) -> Result<Self, Self::Error> {
        if ones >= U32_BITS {
            Err(anyhow::format_err!(
                "failed precondition: argument must be < 32 (bit length of an IPv4 address)",
            ))
        } else {
            Ok(SubnetMask { ones })
        }
    }
}

impl TryFrom<Ipv4Addr> for SubnetMask {
    type Error = anyhow::Error;

    fn try_from(mask: Ipv4Addr) -> Result<Self, Self::Error> {
        let mask: u32 = mask.into();
        let ones = mask.count_ones();
        if ones + mask.trailing_zeros() != 32 {
            Err(anyhow::format_err!("mask did not have consecutive ones {:b}", mask))
        } else {
            Ok(SubnetMask { ones: ones as u8 })
        }
    }
}

impl FidlCompatible<fidl_fuchsia_net::Ipv4Address> for SubnetMask {
    type FromError = anyhow::Error;
    type IntoError = never::Never;

    fn try_from_fidl(fidl: fidl_fuchsia_net::Ipv4Address) -> Result<Self, Self::FromError> {
        let addr = Ipv4Addr::from_fidl(fidl);
        SubnetMask::try_from(addr)
    }

    fn try_into_fidl(self) -> Result<fidl_fuchsia_net::Ipv4Address, Self::IntoError> {
        let addr = Ipv4Addr::from(self.to_u32());
        Ok(addr.into_fidl())
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
    use crate::server::tests::{random_ipv4_generator, random_mac_generator};

    #[test]
    fn test_try_from_ipv4addr_with_consecutive_ones_returns_mask() -> Result<(), anyhow::Error> {
        assert_eq!(SubnetMask::try_from(Ipv4Addr::new(255, 255, 255, 0))?, SubnetMask { ones: 24 });
        Ok(())
    }

    #[test]
    fn test_try_from_ipv4addr_with_nonconsecutive_ones_returns_err() -> Result<(), anyhow::Error> {
        assert!(SubnetMask::try_from(Ipv4Addr::new(255, 255, 255, 1)).is_err());
        Ok(())
    }

    #[test]
    fn test_into_ipv4addr_returns_ipv4addr() -> Result<(), anyhow::Error> {
        let v1: Ipv4Addr = SubnetMask { ones: 24 }.into();
        let v2: Ipv4Addr = SubnetMask { ones: 0 }.into();
        let v3: Ipv4Addr = SubnetMask { ones: 32 }.into();
        assert_eq!(v1, Ipv4Addr::new(255, 255, 255, 0));
        assert_eq!(v2, Ipv4Addr::new(0, 0, 0, 0));
        assert_eq!(v3, Ipv4Addr::new(255, 255, 255, 255));
        Ok(())
    }

    #[test]
    fn test_lease_length_try_from_fidl() {
        let both = fidl_fuchsia_net_dhcp::LeaseLength { default: Some(42), max: Some(42) };
        let with_default = fidl_fuchsia_net_dhcp::LeaseLength { default: Some(42), max: None };
        let with_max = fidl_fuchsia_net_dhcp::LeaseLength { default: None, max: Some(42) };
        let neither = fidl_fuchsia_net_dhcp::LeaseLength { default: None, max: None };

        assert_eq!(
            LeaseLength::try_from_fidl(both).unwrap(),
            LeaseLength { default_seconds: 42, max_seconds: 42 }
        );
        assert_eq!(
            LeaseLength::try_from_fidl(with_default).unwrap(),
            LeaseLength { default_seconds: 42, max_seconds: 42 }
        );
        assert!(LeaseLength::try_from_fidl(with_max).is_err());
        assert!(LeaseLength::try_from_fidl(neither).is_err());
    }

    #[test]
    fn test_managed_addresses_try_from_fidl() -> Result<(), anyhow::Error> {
        let good_mask = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 255, 0] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 254] }),
        };
        let bad_mask = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 0, 255] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 254] }),
        };
        let missing_fields = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: None,
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: None,
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 254] }),
        };
        let invalid_network_id = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 128] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 255, 0] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 254] }),
        };
        let invalid_broadcast = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 1, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 255, 0] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 254] }),
        };
        let invalid_pool_range_start = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 255, 0] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 1, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 254] }),
        };
        let invalid_pool_range_stop = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 255, 0] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 2] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 1, 254] }),
        };
        let start_after_stop = fidl_fuchsia_net_dhcp::AddressPool {
            network_id: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 0] }),
            broadcast: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 255] }),
            mask: Some(fidl_fuchsia_net::Ipv4Address { addr: [255, 255, 255, 0] }),
            pool_range_start: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 20] }),
            pool_range_stop: Some(fidl_fuchsia_net::Ipv4Address { addr: [192, 168, 0, 10] }),
        };

        assert_eq!(
            ManagedAddresses::try_from_fidl(good_mask).unwrap(),
            ManagedAddresses {
                network_id: Ipv4Addr::from([192, 168, 0, 0]),
                broadcast: Ipv4Addr::from([192, 168, 0, 255]),
                mask: SubnetMask::try_from(24)?,
                pool_range_start: Ipv4Addr::from([192, 168, 0, 2]),
                pool_range_stop: Ipv4Addr::from([192, 168, 0, 254]),
            }
        );
        assert!(ManagedAddresses::try_from_fidl(bad_mask).is_err());
        assert!(ManagedAddresses::try_from_fidl(missing_fields).is_err());
        assert!(ManagedAddresses::try_from_fidl(invalid_network_id).is_err());
        assert!(ManagedAddresses::try_from_fidl(invalid_broadcast).is_err());
        assert!(ManagedAddresses::try_from_fidl(invalid_pool_range_start).is_err());
        assert!(ManagedAddresses::try_from_fidl(invalid_pool_range_stop).is_err());
        assert!(ManagedAddresses::try_from_fidl(start_after_stop).is_err());

        Ok(())
    }

    #[test]
    fn test_static_assignments_try_from_fidl() -> Result<(), anyhow::Error> {
        use std::iter::FromIterator;

        let fidl_fuchsia_hardware_ethernet_ext::MacAddress { octets: mac } = random_mac_generator();
        let ip = random_ipv4_generator();
        let fields_present = vec![fidl_fuchsia_net_dhcp::StaticAssignment {
            host: Some(fidl_fuchsia_net::MacAddress { octets: mac.clone() }),
            assigned_addr: Some(ip.into_fidl()),
        }];
        let multiple_entries = vec![
            fidl_fuchsia_net_dhcp::StaticAssignment {
                host: Some(fidl_fuchsia_net::MacAddress { octets: mac.clone() }),
                assigned_addr: Some(ip.into_fidl()),
            },
            fidl_fuchsia_net_dhcp::StaticAssignment {
                host: Some(fidl_fuchsia_net::MacAddress { octets: mac.clone() }),
                assigned_addr: Some(random_ipv4_generator().into_fidl()),
            },
        ];
        let fields_missing =
            vec![fidl_fuchsia_net_dhcp::StaticAssignment { host: None, assigned_addr: None }];

        assert_eq!(
            StaticAssignments::try_from_fidl(fields_present).unwrap(),
            StaticAssignments(HashMap::from_iter(
                vec![(fidl_fuchsia_net_ext::MacAddress { octets: mac }, ip)].into_iter()
            ))
        );
        assert!(StaticAssignments::try_from_fidl(multiple_entries).is_err());
        assert!(StaticAssignments::try_from_fidl(fields_missing).is_err());
        Ok(())
    }
}
