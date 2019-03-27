// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fidl;

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct IpAddress(pub std::net::IpAddr);

impl std::fmt::Display for IpAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let IpAddress(ip_address) = self;
        write!(f, "{}", ip_address)
    }
}

impl From<fidl::IpAddress> for IpAddress {
    fn from(addr: fidl::IpAddress) -> IpAddress {
        IpAddress(match addr {
            fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr }) => addr.into(),
            fidl::IpAddress::Ipv6(fidl::Ipv6Address { addr }) => addr.into(),
        })
    }
}

impl Into<fidl::IpAddress> for IpAddress {
    fn into(self) -> fidl::IpAddress {
        let IpAddress(ip_address) = self;
        match ip_address {
            std::net::IpAddr::V4(v4addr) => fidl::IpAddress::Ipv4(fidl::Ipv4Address {
                addr: v4addr.octets(),
            }),
            std::net::IpAddr::V6(v6addr) => fidl::IpAddress::Ipv6(fidl::Ipv6Address {
                addr: v6addr.octets(),
            }),
        }
    }
}

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct Subnet {
    addr: IpAddress,
    prefix_len: u8,
}

impl std::fmt::Display for Subnet {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let Self { addr, prefix_len } = self;
        write!(f, "{}/{}", addr, prefix_len)
    }
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

        let addr = IpAddress(addr);
        Ok(Subnet {
            addr,
            prefix_len: validated_prefix?,
        })
    }
}

impl From<fidl::Subnet> for Subnet {
    fn from(subnet: fidl::Subnet) -> Self {
        let fidl::Subnet { addr, prefix_len } = subnet;
        let addr = addr.into();
        Self { addr, prefix_len }
    }
}

impl Into<fidl::Subnet> for Subnet {
    fn into(self) -> fidl::Subnet {
        let Self { addr, prefix_len } = self;
        let addr = addr.into();
        fidl::Subnet { addr, prefix_len }
    }
}
