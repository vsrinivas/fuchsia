// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities.

/// Useful constants for tests.
pub(crate) mod constants {
    use fidl_fuchsia_net as fnet;
    use fidl_fuchsia_net_name as fname;

    use crate::DEFAULT_DNS_PORT;

    pub(crate) const UNSPECIFIED_SOURCE_SOCKADDR: fnet::SocketAddress =
        fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: fnet::Ipv4Address { addr: [8, 8, 8, 9] },
            port: DEFAULT_DNS_PORT,
        });

    pub(crate) const UNSPECIFIED_SOURCE_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(UNSPECIFIED_SOURCE_SOCKADDR),
        source: None,
        ..fname::DnsServer_::EMPTY
    };

    pub(crate) const STATIC_SOURCE_SOCKADDR: fnet::SocketAddress =
        fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: fnet::Ipv4Address { addr: [8, 8, 8, 8] },
            port: DEFAULT_DNS_PORT,
        });

    pub(crate) const STATIC_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(STATIC_SOURCE_SOCKADDR),
        source: Some(fname::DnsServerSource::StaticSource(fname::StaticDnsServerSource::EMPTY)),
        ..fname::DnsServer_::EMPTY
    };

    pub(crate) const DHCP_SOURCE_SOCKADDR: fnet::SocketAddress =
        fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: fnet::Ipv4Address { addr: [8, 8, 4, 4] },
            port: DEFAULT_DNS_PORT,
        });

    pub(crate) const DHCP_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(DHCP_SOURCE_SOCKADDR),
        source: Some(fname::DnsServerSource::Dhcp(fname::DhcpDnsServerSource {
            source_interface: Some(1),
            ..fname::DhcpDnsServerSource::EMPTY
        })),
        ..fname::DnsServer_::EMPTY
    };

    pub(crate) const NDP_SOURCE_SOCKADDR: fnet::SocketAddress =
        fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: fnet::Ipv6Address {
                addr: [
                    0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x44, 0x44,
                ],
            },
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        });

    pub(crate) const NDP_SERVER_INTERFACE_ID: u64 = 2;

    pub(crate) const NDP_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(NDP_SOURCE_SOCKADDR),
        source: Some(fname::DnsServerSource::Ndp(fname::NdpDnsServerSource {
            source_interface: Some(NDP_SERVER_INTERFACE_ID),
            ..fname::NdpDnsServerSource::EMPTY
        })),
        ..fname::DnsServer_::EMPTY
    };

    pub(crate) const DHCPV6_SOURCE_SOCKADDR1: fnet::SocketAddress =
        fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: fnet::Ipv6Address {
                addr: [
                    0x20, 0x02, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x44, 0x40,
                ],
            },
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        });

    pub(crate) const DHCPV6_SERVER1_INTERFACE_ID: u64 = 3;

    pub(crate) const DHCPV6_SERVER1: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(DHCPV6_SOURCE_SOCKADDR1),
        source: Some(fname::DnsServerSource::Dhcpv6(fname::Dhcpv6DnsServerSource {
            source_interface: Some(DHCPV6_SERVER1_INTERFACE_ID),
            ..fname::Dhcpv6DnsServerSource::EMPTY
        })),
        ..fname::DnsServer_::EMPTY
    };

    pub(crate) const DHCPV6_SOURCE_SOCKADDR2: fnet::SocketAddress =
        fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: fnet::Ipv6Address {
                addr: [
                    0x20, 0x02, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x44, 0x41,
                ],
            },
            port: DEFAULT_DNS_PORT,
            zone_index: 0,
        });

    pub(crate) const DHCPV6_SERVER2_INTERFACE_ID: u64 = 4;

    pub(crate) const DHCPV6_SERVER2: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(DHCPV6_SOURCE_SOCKADDR2),
        source: Some(fname::DnsServerSource::Dhcpv6(fname::Dhcpv6DnsServerSource {
            source_interface: Some(DHCPV6_SERVER2_INTERFACE_ID),
            ..fname::Dhcpv6DnsServerSource::EMPTY
        })),
        ..fname::DnsServer_::EMPTY
    };
}
