// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_netstack as fidl;

use serde_derive::{Deserialize, Serialize};

pub struct NetAddress(pub std::net::IpAddr);

impl Into<fidl::NetAddress> for NetAddress {
    fn into(self) -> fidl::NetAddress {
        let NetAddress(t) = self;
        match t {
            std::net::IpAddr::V4(v4addr) => fidl::NetAddress {
                family: fidl::NetAddressFamily::Ipv4,
                ipv4: Some(Box::new(fidl::Ipv4Address {
                    addr: v4addr.octets(),
                })),
                ipv6: None,
            },
            std::net::IpAddr::V6(v6addr) => fidl::NetAddress {
                family: fidl::NetAddressFamily::Ipv6,
                ipv4: None,
                ipv6: Some(Box::new(fidl::Ipv6Address {
                    addr: v6addr.octets(),
                })),
            },
        }
    }
}

#[derive(PartialEq, Eq, Serialize, Deserialize, Debug, Clone, Copy)]
pub struct Subnet {
    pub addr: std::net::IpAddr,
    pub prefix_len: u8,
}

impl std::str::FromStr for Subnet {
    type Err = failure::Error;

    // Parse a Subnet from a CIDR-notated IP address.
    //
    // NB: if we need additional CIDR related functionality in the future,
    // we should consider pulling in https://crates.io/crates/cidr
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut pieces = s.split('/');
        let addr = pieces
            .next()
            .expect("String#split should never return an empty iterator")
            .parse::<std::net::IpAddr>()?;

        let addr_len = match addr {
            std::net::IpAddr::V4(_) => 32,
            std::net::IpAddr::V6(_) => 128,
        };
        let validated_prefix = match pieces.next() {
            Some(p) => {
                let parsed_len = p.parse::<u8>()?;
                if parsed_len > addr_len {
                    Err(failure::format_err!(
                        "prefix length provided ({} bits) too large. address {} is only {} bits long",
                        parsed_len,
                        addr,
                        addr_len
                    ))
                } else {
                    Ok(parsed_len)
                }
            }
            None => Ok(addr_len),
        };

        let () = match pieces.next() {
            Some(_) => Err(failure::format_err!(
                "more than one '/' separator found while attempting to parse CIDR string {}",
                s
            )),
            None => Ok(()),
        }?;

        Ok(Subnet {
            addr: addr,
            prefix_len: validated_prefix?,
        })
    }
}

impl Into<fidl::Subnet> for Subnet {
    fn into(self) -> fidl::Subnet {
        fidl::Subnet {
            addr: NetAddress(self.addr).into(),
            prefix_len: self.prefix_len,
        }
    }
}

#[derive(Debug, Eq, PartialEq, Clone, Copy)]
pub enum IpAddressConfig {
    StaticIp(Subnet),
    Dhcp,
}

impl Into<fidl::IpAddressConfig> for IpAddressConfig {
    fn into(self) -> fidl::IpAddressConfig {
        match self {
            IpAddressConfig::Dhcp => fidl::IpAddressConfig::Dhcp(false),
            IpAddressConfig::StaticIp(subnet) => fidl::IpAddressConfig::StaticIp(subnet.into()),
        }
    }
}
