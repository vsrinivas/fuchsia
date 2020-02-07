// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_net as fnet,
    fidl_fuchsia_net_stack::{self as stack},
    fidl_fuchsia_router_config as netconfig,
    std::net::{IpAddr, Ipv4Addr, Ipv6Addr},
};

/// LifIpAddr is an IP address and its prefix.
#[derive(Hash, Eq, PartialEq, Debug, Clone, Copy)]
pub struct LifIpAddr {
    pub address: IpAddr,
    pub prefix: u8,
}

impl From<&fnet::IpAddress> for LifIpAddr {
    fn from(addr: &fnet::IpAddress) -> Self {
        match addr {
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => {
                LifIpAddr { address: IpAddr::from(*addr), prefix: 32 }
            }
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => {
                LifIpAddr { address: IpAddr::from(*addr), prefix: 128 }
            }
        }
    }
}

impl From<&stack::InterfaceAddress> for LifIpAddr {
    fn from(addr: &stack::InterfaceAddress) -> Self {
        LifIpAddr { address: to_ip_addr(addr.ip_address), prefix: addr.prefix_len }
    }
}

impl From<&fnet::Subnet> for LifIpAddr {
    fn from(s: &fnet::Subnet) -> Self {
        match *s {
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }),
                prefix_len: prefix,
            } => LifIpAddr { address: addr.into(), prefix },
            fnet::Subnet {
                addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }),
                prefix_len: prefix,
            } => LifIpAddr { address: addr.into(), prefix },
        }
    }
}

impl From<&netconfig::CidrAddress> for LifIpAddr {
    fn from(a: &netconfig::CidrAddress) -> Self {
        match a.address {
            Some(addr) => {
                LifIpAddr { address: to_ip_addr(addr), prefix: a.prefix_length.unwrap_or(0) }
            }
            None => LifIpAddr { address: IpAddr::from([0, 0, 0, 0]), prefix: 0 },
        }
    }
}

impl From<&LifIpAddr> for netconfig::CidrAddress {
    fn from(addr: &LifIpAddr) -> Self {
        match addr.address {
            IpAddr::V4(a) => netconfig::CidrAddress {
                address: Some(fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: a.octets() })),
                prefix_length: Some(addr.prefix),
            },
            IpAddr::V6(a) => netconfig::CidrAddress {
                address: Some(fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: a.octets() })),
                prefix_length: Some(addr.prefix),
            },
        }
    }
}

impl From<&LifIpAddr> for fnet::Subnet {
    fn from(addr: &LifIpAddr) -> Self {
        match addr.address {
            IpAddr::V4(a) => fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address {
                    addr: (u32::from_be_bytes(a.octets()) >> (32 - addr.prefix)
                        << (32 - addr.prefix))
                        .to_be_bytes(),
                }),
                prefix_len: addr.prefix,
            },
            IpAddr::V6(a) => fnet::Subnet {
                addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                    addr: (u128::from_be_bytes(a.octets()) >> (128 - addr.prefix)
                        << (128 - addr.prefix))
                        .to_be_bytes(),
                }),
                prefix_len: addr.prefix,
            },
        }
    }
}

impl From<&LifIpAddr> for stack::InterfaceAddress {
    fn from(addr: &LifIpAddr) -> Self {
        match addr.address {
            IpAddr::V4(a) => stack::InterfaceAddress {
                ip_address: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: a.octets() }),
                prefix_len: addr.prefix,
            },
            IpAddr::V6(a) => stack::InterfaceAddress {
                ip_address: fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr: a.octets() }),
                prefix_len: addr.prefix,
            },
        }
    }
}

impl LifIpAddr {
    /// Returns true if `address` is in the same subnet as `LifIpAddr`.
    pub fn is_in_same_subnet(&self, address: &IpAddr) -> bool {
        let local_subnet = strip_host(&self.address, self.prefix);
        let address_subnet = strip_host(address, self.prefix);
        local_subnet == address_subnet
    }

    /// Returns [`true`] if this address is an [IPv4 address], and [`false`] otherwise.
    pub fn is_ipv4(&self) -> bool {
        self.address.is_ipv4()
    }

    /// Returns [`true`] if this address is an [IPv6 address], and [`false`] otherwise.
    pub fn is_ipv6(&self) -> bool {
        self.address.is_ipv6()
    }
}

/// Creates an `std::net::IpAddr` from fuchsia.net.IpAddress.
pub fn to_ip_addr(addr: fnet::IpAddress) -> IpAddr {
    match addr {
        fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => IpAddr::from(addr),
        fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => IpAddr::from(addr),
    }
}

/// Converts a subnet mask given as a set of octets to a scalar prefix length.
pub fn subnet_mask_to_prefix_length(addr: fnet::IpAddress) -> u8 {
    match addr {
        fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => {
            (!u32::from_be_bytes(addr)).leading_zeros() as u8
        }
        fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => {
            (!u128::from_be_bytes(addr)).leading_zeros() as u8
        }
    }
}

/// Strips the host part from a given `address` and `prefix`.
fn strip_host(address: &IpAddr, prefix: u8) -> IpAddr {
    match address {
        IpAddr::V4(a) => {
            if prefix == 0 {
                IpAddr::V4(Ipv4Addr::new(0, 0, 0, 0))
            } else if prefix > 32 {
                *address
            } else {
                IpAddr::V4(Ipv4Addr::from(
                    (u32::from_be_bytes(a.octets()) >> (32 - prefix) << (32 - prefix))
                        .to_be_bytes(),
                ))
            }
        }
        IpAddr::V6(a) => {
            if prefix == 0 {
                IpAddr::V6(Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0))
            } else if prefix > 128 {
                *address
            } else {
                IpAddr::V6(Ipv6Addr::from(
                    (u128::from_be_bytes(a.octets()) >> (128 - prefix) << (128 - prefix))
                        .to_be_bytes(),
                ))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn v4(addr: [u8; 4]) -> fnet::IpAddress {
        fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr })
    }

    fn v6(addr: [u8; 16]) -> fnet::IpAddress {
        fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr })
    }

    #[test]
    fn test_to_prefix() {
        assert_eq!(subnet_mask_to_prefix_length(v4([255, 255, 255, 255])), 32);
        assert_eq!(subnet_mask_to_prefix_length(v4([255, 255, 255, 0])), 24);
        assert_eq!(subnet_mask_to_prefix_length(v4([255, 128, 0, 0])), 9);
        assert_eq!(subnet_mask_to_prefix_length(v4([0, 0, 0, 0])), 0);
        assert_eq!(
            subnet_mask_to_prefix_length(v6([
                255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255
            ])),
            128
        );
        assert_eq!(
            subnet_mask_to_prefix_length(v6([
                255, 255, 255, 255, 255, 255, 255, 255, 0, 0, 0, 0, 0, 0, 0, 0,
            ])),
            64
        );
    }

    #[test]
    fn test_from_ipaddress_to_lifipaddr() {
        assert_eq!(
            LifIpAddr::from(&fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr: [1, 2, 3, 4] })),
            LifIpAddr { address: "1.2.3.4".parse().unwrap(), prefix: 32 }
        );
        assert_eq!(
            LifIpAddr::from(&fnet::IpAddress::Ipv6(fnet::Ipv6Address {
                addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0xfc, 0xb6, 0x5b, 0x27, 0xfd, 0x2c, 0xf, 0x12]
            })),
            LifIpAddr { address: "fe80::fcb6:5b27:fd2c:f12".parse().unwrap(), prefix: 128 }
        );
    }

    #[test]
    fn test_strip_host() {
        let got = strip_host(&"85.170.255.170".parse().unwrap(), 23);
        let want: IpAddr = "85.170.254.0".parse().unwrap();
        assert_eq!(want, got, "valid ipv4 prefix");

        let got = strip_host(&"1200:5555:aaaa:aaaa:aaaa:aaaa:5555:aaaa".parse().unwrap(), 57);
        let want: IpAddr = "1200:5555:aaaa:aa80:0:0:0:0".parse().unwrap();
        assert_eq!(want, got, "valid ipv6 prefix");

        let got = strip_host(&"85.170.170.85".parse().unwrap(), 58);
        let want: IpAddr = "85.170.170.85".parse().unwrap();
        assert_eq!(want, got, "invalid ipv4 prefix");

        let got = strip_host(&"1200:0:0:0:aaaa:5555:aaaa:5555".parse().unwrap(), 129);
        let want: IpAddr = "1200:0:0:0:aaaa:5555:aaaa:5555".parse().unwrap();
        assert_eq!(want, got, "invalid ipv6 prefix");

        let got = strip_host(&"85.170.170.85".parse().unwrap(), 0);
        let want: IpAddr = "0.0.0.0".parse().unwrap();
        assert_eq!(want, got, "ipv4 prefix 0");

        let got = strip_host(&"1200:0:0:0:aaaa:5555:aaaa:5555".parse().unwrap(), 0);
        let want: IpAddr = "::".parse().unwrap();
        assert_eq!(want, got, "ipv6 prefix 0");
    }

    #[test]
    fn test_is_in_same_subnet() {
        let address = LifIpAddr { address: "1.2.3.26".parse().unwrap(), prefix: 27 };
        assert!(address.is_in_same_subnet(&"1.2.3.26".parse().unwrap()));
        assert!(address.is_in_same_subnet(&"1.2.3.30".parse().unwrap()));
        assert!(!address.is_in_same_subnet(&"1.2.3.32".parse().unwrap()));
        let address = LifIpAddr {
            address: "2401:fa00:480:16:1295:6946:837:373a".parse().unwrap(),
            prefix: 58,
        };
        assert!(address.is_in_same_subnet(&"2401:fa00:480:16:1295:6946:837:373a".parse().unwrap()));
        assert!(address.is_in_same_subnet(&"2401:fa00:480:16:2345:6946:837:373a".parse().unwrap()));
        assert!(address.is_in_same_subnet(&"2401:fa00:480:26:1295:6946:837:373a".parse().unwrap()));
        assert!(!address.is_in_same_subnet(&"2401:fa00:480:46:2345:6946:837:373a".parse().unwrap()));
    }
}
