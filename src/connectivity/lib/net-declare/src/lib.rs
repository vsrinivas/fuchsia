// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Declares an [`std::net::IpAddr`] from a parsable IP address (either V4 or
/// V6) string.
pub use net_declare_macros::std_ip;
/// Declares an [`std::net::Ipv4Addr`] from a parsable IPv4 address string.
pub use net_declare_macros::std_ip_v4;
/// Declares an [`std::net::Ipv6Addr`] from a parsable IPv6 address string.
pub use net_declare_macros::std_ip_v6;
/// Declares an [`std::net::SocketAddr`] from a parsable IP address + port
/// string (either V4 or V6).
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `scope_id=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
pub use net_declare_macros::std_socket_addr;
/// Declares an [`std::net::SocketAddrV4`] from a parsable IPv4 address + port
/// in the form `addr:port`.
pub use net_declare_macros::std_socket_addr_v4;
/// Declares an [`std::net::SocketAddrV6`] from a parsable IPv6 address + port
/// in the form `[addr]:port`.
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `scope_id=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
pub use net_declare_macros::std_socket_addr_v6;

/// Declares a [`fidl_fuchsia_net::IpAddress`] from a parsable IP address
/// (either V4 or V6) string.
pub use net_declare_macros::fidl_ip;
/// Declares a [`fidl_fuchsia_net::Ipv4Address`] from a parsable IPv4 address
/// string.
pub use net_declare_macros::fidl_ip_v4;
/// Declares a [`fidl_fuchsia_net::Ipv4AddressWithPrefix`] from a parsable IPv4
/// + prefix length string, e.g. `192.168.0.1/24`.
pub use net_declare_macros::fidl_ip_v4_with_prefix;
/// Declares a [`fidl_fuchsia_net::Ipv6Address`] from a parsable IPv6 address
/// string.
pub use net_declare_macros::fidl_ip_v6;
/// Declares a [`fidl_fuchsia_net::MacAddress`] from a parsable MAC address in
/// the form `aa:bb:cc:dd:ee:ff`.
pub use net_declare_macros::fidl_mac;
/// Declares an [`fidl_fuchsia_net::SocketAddress`] from a parsable IP address +
/// port string (either V4 or V6).
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `zone_index=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
pub use net_declare_macros::fidl_socket_addr;
/// Declares a [`fidl_fuchsia_net::Ipv4SocketAddress`] from a parsable IPv4
/// address + port in the form `addr:port`.
pub use net_declare_macros::fidl_socket_addr_v4;
/// Declares a [`fidl_fuchsia_net::Ipv6SocketAddress`] from a parsable IPv6
/// address + port in the form `[addr]:port`.
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `scope_id=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
pub use net_declare_macros::fidl_socket_addr_v6;
/// Declares a [`fidl_fuchsia_net::Subnet`] from a parsable CIDR address string
/// in the form `addr/prefix`, e.g. `192.168.0.1/24` or `ff08::1/64`.
pub use net_declare_macros::fidl_subnet;

/// Declares a [`net_types::ip::IpAddr`] from a parsable IP address (either V4
/// or V6) string.
pub use net_declare_macros::net_ip;
/// Declares a [`net_types::ip::Ipv4Addr`] from a parsable IPv4 address string.
pub use net_declare_macros::net_ip_v4;
/// Declares a [`net_types::ip::Ipv6Addr`] from a parsable IPv6 address string.
pub use net_declare_macros::net_ip_v6;
/// Declares a [`net_types::ethernet::Mac`] from a parsable MAC address in
/// the form `aa:bb:cc:dd:ee:ff`.
pub use net_declare_macros::net_mac;

/// Redeclaration of macros to generate `std` types.
pub mod std {
    pub use super::std_ip as ip;
    pub use super::std_ip_v4 as ip_v4;
    pub use super::std_ip_v6 as ip_v6;
    pub use super::std_socket_addr as socket_addr;
    pub use super::std_socket_addr_v4 as socket_addr_v4;
    pub use super::std_socket_addr_v6 as socket_addr_v6;
}

/// Redeclaration of macros to generate `fidl` types.
pub mod fidl {
    pub use super::fidl_ip as ip;
    pub use super::fidl_ip_v4 as ip_v4;
    pub use super::fidl_ip_v6 as ip_v6;
    pub use super::fidl_mac as mac;
    pub use super::fidl_socket_addr as socket_addr;
    pub use super::fidl_socket_addr_v4 as socket_addr_v4;
    pub use super::fidl_socket_addr_v6 as socket_addr_v6;
    pub use super::fidl_subnet as subnet;
}

/// Redeclaration of macros to generate `net_types` types.
pub mod net {
    pub use super::net_ip as ip;
    pub use super::net_ip_v4 as ip_v4;
    pub use super::net_ip_v6 as ip_v6;
    pub use super::net_mac as mac;
}

#[cfg(test)]
mod tests {
    use super::*;
    use ::std;
    use fidl_fuchsia_net as fidl;

    #[test]
    fn test_std_ip() {
        assert_eq!(
            std::net::IpAddr::V4(std::net::Ipv4Addr::new(192, 168, 0, 1)),
            std_ip!("192.168.0.1")
        );
        assert_eq!(
            std::net::IpAddr::V6(std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102)),
            std_ip!("ff01::0102")
        );
    }

    #[test]
    fn test_std_ip_v4() {
        assert_eq!(std::net::Ipv4Addr::new(192, 168, 0, 1), std_ip_v4!("192.168.0.1"));
    }

    #[test]
    fn test_std_ip_v6() {
        assert_eq!(
            std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102),
            std_ip_v6!("ff01::0102")
        );
    }

    #[test]
    fn test_std_socket_addr() {
        assert_eq!(
            std::net::SocketAddr::V4(std::net::SocketAddrV4::new(
                std::net::Ipv4Addr::new(192, 168, 0, 1),
                8080
            )),
            std_socket_addr!("192.168.0.1:8080")
        );
        assert_eq!(
            std::net::SocketAddr::V6(std::net::SocketAddrV6::new(
                std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102),
                8080,
                0,
                0
            )),
            std_socket_addr!("[ff01::0102]:8080")
        );
    }

    #[test]
    fn test_std_socket_addr_v4() {
        assert_eq!(
            std::net::SocketAddrV4::new(std::net::Ipv4Addr::new(192, 168, 0, 1), 8080),
            std_socket_addr_v4!("192.168.0.1:8080")
        );
    }

    #[test]
    fn test_std_socket_addr_v6() {
        assert_eq!(
            std::net::SocketAddrV6::new(
                std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102),
                8080,
                0,
                0
            ),
            std_socket_addr_v6!("[ff01::0102]:8080")
        );
    }

    #[test]
    fn test_fidl_ip() {
        assert_eq!(
            fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [192, 168, 0, 1] }),
            fidl_ip!("192.168.0.1")
        );

        assert_eq!(
            fidl::IpAddress::Ipv6(fidl::Ipv6Address {
                addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
            }),
            fidl_ip!("ff01::0102")
        );
    }

    #[test]
    fn test_fidl_ip_v4() {
        assert_eq!(fidl::Ipv4Address { addr: [192, 168, 0, 1] }, fidl_ip_v4!("192.168.0.1"));
    }

    #[test]
    fn test_fidl_ip_v6() {
        assert_eq!(
            fidl::Ipv6Address {
                addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
            },
            fidl_ip_v6!("ff01::0102")
        );
    }

    #[test]
    fn test_fidl_socket_addr() {
        assert_eq!(
            fidl::SocketAddress::Ipv4(fidl::Ipv4SocketAddress {
                address: fidl::Ipv4Address { addr: [192, 168, 0, 1] },
                port: 8080
            }),
            fidl_socket_addr!("192.168.0.1:8080")
        );

        assert_eq!(
            fidl::SocketAddress::Ipv6(fidl::Ipv6SocketAddress {
                address: fidl::Ipv6Address {
                    addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
                },
                port: 8080,
                zone_index: 0,
            }),
            fidl_socket_addr!("[ff01::0102]:8080")
        );
    }

    #[test]
    fn test_fidl_socket_addr_v4() {
        assert_eq!(
            fidl::Ipv4SocketAddress {
                address: fidl::Ipv4Address { addr: [192, 168, 0, 1] },
                port: 8080
            },
            fidl_socket_addr_v4!("192.168.0.1:8080")
        );
    }

    #[test]
    fn test_fidl_socket_addr_v6() {
        assert_eq!(
            fidl::Ipv6SocketAddress {
                address: fidl::Ipv6Address {
                    addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
                },
                port: 8080,
                zone_index: 0,
            },
            fidl_socket_addr_v6!("[ff01::0102]:8080")
        );
    }

    #[test]
    fn test_fidl_mac() {
        assert_eq!(fidl::MacAddress { octets: [0, 1, 2, 3, 4, 5] }, fidl_mac!("00:01:02:03:04:05"));
    }

    #[test]
    fn test_accept_quotes() {
        // Rustfmt gets confused with this syntax sometimes, so we allow macros
        // to receive what looks like a string literal as well.
        assert_eq!(
            fidl::MacAddress { octets: [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF] },
            fidl_mac!("AA:BB:CC:DD:EE:FF")
        );
    }

    #[test]
    fn test_fidl_ip_v4_with_prefix() {
        assert_eq!(
            fidl::Ipv4AddressWithPrefix {
                addr: fidl::Ipv4Address { addr: [192, 168, 0, 1] },
                prefix_len: 24
            },
            fidl_ip_v4_with_prefix!("192.168.0.1/24")
        );
    }

    #[test]
    fn test_fidl_subnet_v4() {
        assert_eq!(
            fidl::Subnet {
                addr: fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [192, 168, 0, 1] }),
                prefix_len: 24
            },
            fidl_subnet!("192.168.0.1/24")
        );
    }

    #[test]
    fn test_fidl_subnet_v6() {
        assert_eq!(
            fidl::Subnet {
                addr: fidl::IpAddress::Ipv6(fidl::Ipv6Address {
                    addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
                }),
                prefix_len: 64
            },
            fidl_subnet!("ff01::0102/64")
        );
    }

    #[test]
    fn test_net_ip() {
        assert_eq!(
            net_types::ip::IpAddr::from(net_types::ip::Ipv4Addr::new([192, 168, 0, 1])),
            net_ip!("192.168.0.1")
        );

        assert_eq!(
            net_types::ip::IpAddr::from(net_types::ip::Ipv6Addr::new([
                0xFF01, 0, 0, 0, 0, 0, 0, 0x0102
            ])),
            net_ip!("ff01::0102"),
        );
    }

    #[test]
    fn test_net_ip_v4() {
        assert_eq!(net_types::ip::Ipv4Addr::new([192, 168, 0, 1]), net_ip_v4!("192.168.0.1"),);
    }

    #[test]
    fn test_net_ip_v6() {
        assert_eq!(
            net_types::ip::Ipv6Addr::new([0xFF01, 0, 0, 0, 0, 0, 0, 0x0102]),
            net_ip_v6!("ff01::0102"),
        );
    }

    #[test]
    fn test_net_mac() {
        assert_eq!(
            net_types::ethernet::Mac::new([0, 1, 2, 3, 4, 5]),
            net_mac!("00:01:02:03:04:05")
        );
    }
}
