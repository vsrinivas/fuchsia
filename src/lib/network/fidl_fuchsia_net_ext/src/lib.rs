// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Extensions for types in the `fidl_fuchsia_net` crate.

use std::convert::TryInto as _;

use fidl_fuchsia_net as fidl;

use anyhow;
use net_types::ip;

/// Extension to IP types.
pub trait IpExt {
    /// Is the address a unicast and link-local address?
    fn is_unicast_linklocal(&self) -> bool;
}

impl IpExt for fidl::Ipv6Address {
    fn is_unicast_linklocal(&self) -> bool {
        ip::Ipv6Addr::new(self.addr).is_unicast_linklocal()
    }
}

/// A manual implementation of `From`.
pub trait FromExt<T> {
    /// Performs the conversion.
    fn from_ext(f: T) -> Self;
}

/// A manual implementation of `Into`.
///
/// A blanket implementation is provided for implementers of `FromExt<T>`.
pub trait IntoExt<T> {
    /// Performs the conversion.
    fn into_ext(self) -> T;
}

impl<T, U> IntoExt<U> for T
where
    U: FromExt<T>,
{
    fn into_ext(self) -> U {
        U::from_ext(self)
    }
}

impl FromExt<fidl::Ipv4Address> for fidl::IpAddress {
    fn from_ext(f: fidl::Ipv4Address) -> fidl::IpAddress {
        fidl::IpAddress::Ipv4(f)
    }
}

impl FromExt<fidl::Ipv6Address> for fidl::IpAddress {
    fn from_ext(f: fidl::Ipv6Address) -> fidl::IpAddress {
        fidl::IpAddress::Ipv6(f)
    }
}

impl FromExt<fidl::Ipv4SocketAddress> for fidl::SocketAddress {
    fn from_ext(f: fidl::Ipv4SocketAddress) -> fidl::SocketAddress {
        fidl::SocketAddress::Ipv4(f)
    }
}

impl FromExt<fidl::Ipv6SocketAddress> for fidl::SocketAddress {
    fn from_ext(f: fidl::Ipv6SocketAddress) -> fidl::SocketAddress {
        fidl::SocketAddress::Ipv6(f)
    }
}

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct IpAddress(pub std::net::IpAddr);

impl std::fmt::Display for IpAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
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
            std::net::IpAddr::V4(v4addr) => {
                fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: v4addr.octets() })
            }
            std::net::IpAddr::V6(v6addr) => {
                fidl::IpAddress::Ipv6(fidl::Ipv6Address { addr: v6addr.octets() })
            }
        }
    }
}

impl std::str::FromStr for IpAddress {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(IpAddress(s.parse()?))
    }
}

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct Subnet {
    pub addr: IpAddress,
    pub prefix_len: u8,
}

impl std::fmt::Display for Subnet {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> Result<(), std::fmt::Error> {
        let Self { addr, prefix_len } = self;
        write!(f, "{}/{}", addr, prefix_len)
    }
}

impl std::str::FromStr for Subnet {
    type Err = anyhow::Error;

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
                    Err(anyhow::format_err!(
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
            Some(_) => Err(anyhow::format_err!(
                "more than one '/' separator found while attempting to parse CIDR string {}",
                s
            )),
            None => Ok(()),
        }?;
        let addr = IpAddress(addr);
        Ok(Subnet { addr, prefix_len: validated_prefix? })
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

#[derive(PartialEq, Eq, Debug, Clone, Copy, Hash)]
pub struct MacAddress {
    pub octets: [u8; 6],
}

impl From<fidl::MacAddress> for MacAddress {
    fn from(fidl::MacAddress { octets }: fidl::MacAddress) -> Self {
        Self { octets }
    }
}

impl Into<fidl::MacAddress> for MacAddress {
    fn into(self) -> fidl::MacAddress {
        let Self { octets } = self;
        fidl::MacAddress { octets }
    }
}

impl std::fmt::Display for MacAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Self { octets } = self;
        for (i, byte) in octets.iter().enumerate() {
            if i > 0 {
                write!(f, ":")?;
            }
            write!(f, "{:02x}", byte)?;
        }
        Ok(())
    }
}

impl serde::Serialize for MacAddress {
    fn serialize<S: serde::Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.collect_str(&self)
    }
}

impl<'de> serde::Deserialize<'de> for MacAddress {
    fn deserialize<D: serde::Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        let s = <String as serde::Deserialize>::deserialize(deserializer)?;
        <Self as std::str::FromStr>::from_str(&s).map_err(serde::de::Error::custom)
    }
}

impl std::str::FromStr for MacAddress {
    type Err = anyhow::Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        use anyhow::Context;

        let mut octets = [0; 6];
        let mut iter = s.split(':');
        for (i, octet) in octets.iter_mut().enumerate() {
            let next_octet = iter.next().ok_or_else(|| {
                anyhow::format_err!("MAC address [{}] only specifies {} out of 6 octets", s, i)
            })?;
            *octet = u8::from_str_radix(next_octet, 16)
                .with_context(|| format!("could not parse hex integer from {}", next_octet))?;
        }
        if iter.next().is_some() {
            return Err(anyhow::format_err!("MAC address has more than six octets: {}", s));
        }
        Ok(MacAddress { octets })
    }
}

#[derive(PartialEq, Eq, Debug, Clone, Copy)]
pub struct SocketAddress(pub std::net::SocketAddr);

impl std::fmt::Display for SocketAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let Self(socket_addr) = self;
        match socket_addr {
            std::net::SocketAddr::V4(v4) => std::fmt::Display::fmt(v4, f),
            std::net::SocketAddr::V6(v6) => {
                match v6.scope_id() {
                    0 => std::fmt::Display::fmt(v6, f),
                    // TODO(https://github.com/rust-lang/rust/pull/77426): remove this.
                    scope_id => write!(f, "[{}%{}]:{}", v6.ip(), scope_id, v6.port()),
                }
            }
        }
    }
}

impl From<fidl::SocketAddress> for SocketAddress {
    fn from(f: fidl::SocketAddress) -> Self {
        Self(match f {
            fidl::SocketAddress::Ipv4(fidl::Ipv4SocketAddress {
                address: fidl::Ipv4Address { addr },
                port,
            }) => std::net::SocketAddr::V4(std::net::SocketAddrV4::new(addr.into(), port)),
            fidl::SocketAddress::Ipv6(fidl::Ipv6SocketAddress {
                address: fidl::Ipv6Address { addr },
                port,
                zone_index,
            }) => std::net::SocketAddr::V6(std::net::SocketAddrV6::new(
                addr.into(),
                port,
                0,
                zone_index.try_into().unwrap_or(0),
            )),
        })
    }
}

impl Into<fidl::SocketAddress> for SocketAddress {
    fn into(self) -> fidl::SocketAddress {
        match self.0 {
            std::net::SocketAddr::V4(socket_addr) => {
                fidl::SocketAddress::Ipv4(fidl::Ipv4SocketAddress {
                    address: fidl::Ipv4Address { addr: socket_addr.ip().octets() },
                    port: socket_addr.port(),
                })
            }
            std::net::SocketAddr::V6(socket_addr) => {
                fidl::SocketAddress::Ipv6(fidl::Ipv6SocketAddress {
                    address: fidl::Ipv6Address { addr: socket_addr.ip().octets() },
                    port: socket_addr.port(),
                    zone_index: socket_addr.scope_id().into(),
                })
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::collections::HashMap;
    use std::str::FromStr;

    #[test]
    fn test_from_into_ext() {
        let a = fidl::Ipv4Address { addr: [0; 4] };
        assert_eq!(fidl::IpAddress::Ipv4(a), a.into_ext());

        let a = fidl::Ipv6Address { addr: [0; 16] };
        assert_eq!(fidl::IpAddress::Ipv6(a), a.into_ext());

        let a = fidl::Ipv4SocketAddress { address: fidl::Ipv4Address { addr: [0; 4] }, port: 1 };
        assert_eq!(fidl::SocketAddress::Ipv4(a), a.into_ext());

        let a = fidl::Ipv6SocketAddress {
            address: fidl::Ipv6Address { addr: [0; 16] },
            port: 1,
            zone_index: 2,
        };
        assert_eq!(fidl::SocketAddress::Ipv6(a), a.into_ext());
    }

    #[test]
    fn test_ipaddr() {
        let want_ext = IpAddress(std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, 4)));
        let want_fidl = fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [1, 2, 3, 4] });
        let got_fidl: fidl::IpAddress = want_ext.into();
        let got_ext = IpAddress::from(got_fidl);

        assert_eq!(want_ext, got_ext);
        assert_eq!(want_fidl, got_fidl);
    }

    #[test]
    fn test_subnet() {
        let err_str_subnets = vec![
            // Note "1.2.3.4" or "::" is a valid form. Subnet's FromStr trait allows
            // missing prefix, and assumes the legally maximum prefix length.
            "",
            "/32",                              // no ip address
            " /32",                             // no ip address
            "1.2.3.4/8/8",                      // too many slashes
            "1.2.3.4/33",                       // prefix too long
            "192.168.32.1:8080",                // that's a port, not a prefix
            "e80::e1bf:4fe9:fb62:e3f4/129",     // prefix too long
            "e80::e1bf:4fe9:fb62:e3f4/32%eth0", // zone index
        ];
        for e in err_str_subnets {
            if Subnet::from_str(e).is_ok() {
                eprintln!(
                    "a malformed str is wrongfully convertitable to Subnet struct: \"{}\"",
                    e
                );
                assert!(false);
            }
        }

        let want_str = "1.2.3.4/18";
        let want_ext = Subnet {
            addr: IpAddress(std::net::IpAddr::V4(std::net::Ipv4Addr::new(1, 2, 3, 4))),
            prefix_len: 18,
        };
        let want_fidl = fidl::Subnet {
            addr: fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [1, 2, 3, 4] }),
            prefix_len: 18,
        };

        let got_ext = Subnet::from_str(want_str).ok().expect("conversion error");
        let got_fidl: fidl::Subnet = got_ext.into();
        let got_ext_back = Subnet::from(got_fidl);
        let got_str = &format!("{}", got_ext_back);

        assert_eq!(want_ext, got_ext);
        assert_eq!(want_fidl, got_fidl);
        assert_eq!(got_ext, got_ext_back);
        assert_eq!(want_str, got_str);
    }

    #[test]
    fn mac_addr_from_str_with_valid_str_returns_mac_addr() {
        let result = MacAddress::from_str("AA:BB:CC:DD:EE:FF").unwrap();
        let expected = MacAddress { octets: [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF] };

        assert_eq!(expected, result);
    }

    #[test]
    fn mac_addr_from_str_with_invalid_digit_returns_err() {
        let result = MacAddress::from_str("11:22:33:44:55:GG");

        assert!(result.is_err());
    }

    #[test]
    fn mac_addr_from_str_with_invalid_format_returns_err() {
        let result = MacAddress::from_str("11-22-33-44-55-66");

        assert!(result.is_err());
    }

    #[test]
    fn mac_addr_from_str_with_empty_string_returns_err() {
        let result = MacAddress::from_str("");

        assert!(result.is_err());
    }

    #[test]
    fn mac_addr_from_str_with_extra_quotes_returns_err() {
        let result = MacAddress::from_str("\"11:22:33:44:55:66\"");

        assert!(result.is_err());
    }

    #[test]
    fn valid_mac_addr_array_deserializes_to_vec_of_mac_addrs() {
        let result: Vec<MacAddress> =
            serde_json::from_str("[\"11:11:11:11:11:11\", \"AA:AA:AA:AA:AA:AA\"]").unwrap();
        let expected = vec![
            MacAddress { octets: [0x11, 0x11, 0x11, 0x11, 0x11, 0x11] },
            MacAddress { octets: [0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA] },
        ];

        assert_eq!(expected, result);
    }

    #[test]
    fn mac_addr_to_mac_addr_map_deserializes_to_hashmap() {
        let result: HashMap<MacAddress, MacAddress> =
            serde_json::from_str("{\"11:22:33:44:55:66\": \"AA:BB:CC:DD:EE:FF\"}").unwrap();
        let mut expected = HashMap::new();
        expected.insert(
            MacAddress { octets: [0x11, 0x22, 0x33, 0x44, 0x55, 0x66] },
            MacAddress { octets: [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF] },
        );

        assert_eq!(expected, result);
    }

    #[test]
    fn mac_addr_to_mac_addr_map_serializes_to_valid_json() {
        let mut mac_addr_map = HashMap::new();
        mac_addr_map.insert(
            MacAddress { octets: [0x11, 0x22, 0x33, 0x44, 0x55, 0x66] },
            MacAddress { octets: [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF] },
        );

        let result = serde_json::to_string(&mac_addr_map).unwrap();

        assert_eq!("{\"11:22:33:44:55:66\":\"aa:bb:cc:dd:ee:ff\"}", result);
    }

    #[test]
    fn test_socket_addr() {
        // V4.
        let want_ext = SocketAddress(std::net::SocketAddr::V4(std::net::SocketAddrV4::new(
            std::net::Ipv4Addr::new(1, 2, 3, 4),
            5,
        )));
        let want_fidl = fidl::SocketAddress::Ipv4(fidl::Ipv4SocketAddress {
            address: fidl::Ipv4Address { addr: [1, 2, 3, 4] },
            port: 5,
        });
        let got_fidl: fidl::SocketAddress = want_ext.into();
        let got_ext = SocketAddress::from(want_fidl);

        assert_eq!(want_ext, got_ext);
        assert_eq!(want_fidl, got_fidl);

        // V6.
        let want_ext = SocketAddress(std::net::SocketAddr::V6(std::net::SocketAddrV6::new(
            std::net::Ipv6Addr::new(0x0102, 0x0304, 0x0506, 0x0708, 0x090A, 0x0B0C, 0x0D0E, 0x0F10),
            17,
            0,
            18,
        )));
        let want_fidl = fidl::SocketAddress::Ipv6(fidl::Ipv6SocketAddress {
            address: fidl::Ipv6Address {
                addr: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
            },
            port: 17,
            zone_index: 18,
        });
        let got_fidl: fidl::SocketAddress = want_ext.into();
        let got_ext = SocketAddress::from(got_fidl);

        assert_eq!(want_ext, got_ext);
        assert_eq!(want_fidl, want_fidl);
    }
}
