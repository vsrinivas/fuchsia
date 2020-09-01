// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use proc_macro_hack::proc_macro_hack;

/// Declares an [`std::net::IpAddr`] from a parsable IP address (either V4 or
/// V6) string.
#[proc_macro_hack]
pub use net_declare_macros::std_ip;
/// Declares an [`std::net::Ipv4Addr`] from a parsable IPv4 address string.
#[proc_macro_hack]
pub use net_declare_macros::std_ip_v4;
/// Declares an [`std::net::Ipv6Addr`] from a parsable IPv6 address string.
#[proc_macro_hack]
pub use net_declare_macros::std_ip_v6;
/// Declares an [`std::net::SocketAddr`] from a parsable IP address + port
/// string (either V4 or V6).
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `scope_id=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
#[proc_macro_hack]
pub use net_declare_macros::std_socket_addr;
/// Declares an [`std::net::SocketAddrV4`] from a parsable IPv4 address + port
/// in the form `addr:port`.
#[proc_macro_hack]
pub use net_declare_macros::std_socket_addr_v4;
/// Declares an [`std::net::SocketAddrV6`] from a parsable IPv6 address + port
/// in the form `[addr]:port`.
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `scope_id=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
#[proc_macro_hack]
pub use net_declare_macros::std_socket_addr_v6;

/// Declares a [`fidl_fuchsia_net::IpAddress`] from a parsable IP address
/// (either V4 or V6) string.
#[proc_macro_hack]
pub use net_declare_macros::fidl_ip;
/// Declares a [`fidl_fuchsia_net::Ipv4Address`] from a parsable IPv4 address
/// string.
#[proc_macro_hack]
pub use net_declare_macros::fidl_ip_v4;
/// Declares a [`fidl_fuchsia_net::Ipv6Address`] from a parsable IPv6 address
/// string.
#[proc_macro_hack]
pub use net_declare_macros::fidl_ip_v6;
/// Declares a [`fidl_fuchsia_net::MacAddress`] from a parsable MAC address in
/// the form "aa:bb:cc:dd:ee:ff".
#[proc_macro_hack]
pub use net_declare_macros::fidl_mac;
/// Declares an [`fidl_fuchsia_net::SocketAddress`] from a parsable IP address +
/// port string (either V4 or V6).
///
/// NOTE: `std::net::SocketAddrV6` does not support parsing scope_id from
/// strings, meaning the generated IPv6 socket address will always have
/// `zone_index=0`. See [Rust issue 1992].
///
/// [Rust issue 1992]: https://github.com/rust-lang/rfcs/issues/1992
#[proc_macro_hack]
pub use net_declare_macros::fidl_socket_addr;
#[proc_macro_hack]
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
#[proc_macro_hack]
pub use net_declare_macros::fidl_socket_addr_v6;

/// Redeclaration of macros to generate `std` types.
pub mod std {
    pub use std_ip as ip;
    pub use std_ip_v4 as ip_v4;
    pub use std_ip_v6 as ip_v6;
    pub use std_socket_addr as socket_addr;
    pub use std_socket_addr_v4 as socket_addr_v4;
    pub use std_socket_addr_v6 as socket_addr_v6;
}

/// Redeclaration of macros to generate `fidl` types.
pub mod fidl {
    pub use fidl_ip as ip;
    pub use fidl_ip_v4 as ip_v4;
    pub use fidl_ip_v6 as ip_v6;
    pub use fidl_mac as mac;
    pub use fidl_socket_addr as socket_addr;
    pub use fidl_socket_addr_v4 as socket_addr_v4;
    pub use fidl_socket_addr_v6 as socket_addr_v6;
}

#[cfg(test)]
mod tests {
    use fidl_fuchsia_net as fidl;

    #[test]
    fn test_std_ip() {
        assert_eq!(
            std::net::IpAddr::V4(std::net::Ipv4Addr::new(192, 168, 0, 1)),
            std_ip!(192.168.0.1)
        );
        assert_eq!(
            std::net::IpAddr::V6(std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102)),
            std_ip!(ff01::0102)
        );
    }

    #[test]
    fn test_std_ip_v4() {
        assert_eq!(std::net::Ipv4Addr::new(192, 168, 0, 1), std_ip_v4!(192.168.0.1));
    }

    #[test]
    fn test_std_ip_v6() {
        assert_eq!(
            std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102),
            std_ip_v6!(ff01::0102)
        );
    }

    #[test]
    fn test_std_socket_addr() {
        assert_eq!(
            std::net::SocketAddr::V4(std::net::SocketAddrV4::new(
                std::net::Ipv4Addr::new(192, 168, 0, 1),
                8080
            )),
            std_socket_addr!(192.168.0.1:8080)
        );
        assert_eq!(
            std::net::SocketAddr::V6(std::net::SocketAddrV6::new(
                std::net::Ipv6Addr::new(0xFF01, 0, 0, 0, 0, 0, 0, 0x0102),
                8080,
                0,
                0
            )),
            std_socket_addr!([ff01::0102]:8080)
        );
    }

    #[test]
    fn test_std_socket_addr_v4() {
        assert_eq!(
            std::net::SocketAddrV4::new(std::net::Ipv4Addr::new(192, 168, 0, 1), 8080),
            std_socket_addr_v4!(192.168.0.1:8080)
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
            std_socket_addr_v6!([ff01::0102]:8080)
        );
    }

    #[test]
    fn test_fidl_ip() {
        assert_eq!(
            fidl::IpAddress::Ipv4(fidl::Ipv4Address { addr: [192, 168, 0, 1] }),
            fidl_ip!(192.168.0.1)
        );

        assert_eq!(
            fidl::IpAddress::Ipv6(fidl::Ipv6Address {
                addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
            }),
            fidl_ip!(ff01::0102)
        );
    }

    #[test]
    fn test_fidl_ip_v4() {
        assert_eq!(fidl::Ipv4Address { addr: [192, 168, 0, 1] }, fidl_ip_v4!(192.168.0.1));
    }

    #[test]
    fn test_fidl_ip_v6() {
        assert_eq!(
            fidl::Ipv6Address {
                addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
            },
            fidl_ip_v6!(ff01::0102)
        );
    }

    #[test]
    fn test_fidl_socket_addr() {
        assert_eq!(
            fidl::SocketAddress::Ipv4(fidl::Ipv4SocketAddress {
                address: fidl::Ipv4Address { addr: [192, 168, 0, 1] },
                port: 8080
            }),
            fidl_socket_addr!(192.168.0.1:8080)
        );

        assert_eq!(
            fidl::SocketAddress::Ipv6(fidl::Ipv6SocketAddress {
                address: fidl::Ipv6Address {
                    addr: [0xFF, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x01, 0x02]
                },
                port: 8080,
                zone_index: 0,
            }),
            fidl_socket_addr!([ff01::0102]:8080)
        );
    }

    #[test]
    fn test_fidl_socket_addr_v4() {
        assert_eq!(
            fidl::Ipv4SocketAddress {
                address: fidl::Ipv4Address { addr: [192, 168, 0, 1] },
                port: 8080
            },
            fidl_socket_addr_v4!(192.168.0.1:8080)
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
            fidl_socket_addr_v6!([ff01::0102]:8080)
        );
    }

    #[test]
    fn test_fidl_mac() {
        assert_eq!(fidl::MacAddress { octets: [0, 1, 2, 3, 4, 5] }, fidl_mac!(00:01:02:03:04:05));
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
}
