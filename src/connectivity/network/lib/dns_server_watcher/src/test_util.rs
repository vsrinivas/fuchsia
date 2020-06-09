// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Test utilities.

/// Useful constants for tests.
pub(crate) mod constants {
    use fidl_fuchsia_net as fnet;
    use fidl_fuchsia_net_name as fname;

    use crate::DEFAULT_DNS_PORT;

    pub(crate) const UNSPECIFIED_SOURCE_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: fnet::Ipv4Address { addr: [8, 8, 8, 9] },
            port: DEFAULT_DNS_PORT,
        })),
        source: None,
    };

    pub(crate) const STATIC_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: fnet::Ipv4Address { addr: [8, 8, 8, 8] },
            port: DEFAULT_DNS_PORT,
        })),
        source: Some(fname::DnsServerSource::StaticSource(fname::StaticDnsServerSource {})),
    };

    pub(crate) const DHCP_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(fnet::SocketAddress::Ipv4(fnet::Ipv4SocketAddress {
            address: fnet::Ipv4Address { addr: [8, 8, 4, 4] },
            port: DEFAULT_DNS_PORT,
        })),
        source: Some(fname::DnsServerSource::Dhcp(fname::DhcpDnsServerSource {
            source_interface: Some(1),
        })),
    };

    pub(crate) const NDP_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: fnet::Ipv6Address {
                addr: [
                    0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x44, 0x44,
                ],
            },
            port: DEFAULT_DNS_PORT,
            zone_index: 2,
        })),
        source: Some(fname::DnsServerSource::Ndp(fname::NdpDnsServerSource {
            source_interface: Some(2),
        })),
    };

    pub(crate) const DHCPV6_SERVER: fname::DnsServer_ = fname::DnsServer_ {
        address: Some(fnet::SocketAddress::Ipv6(fnet::Ipv6SocketAddress {
            address: fnet::Ipv6Address {
                addr: [
                    0x20, 0x02, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                    0x00, 0x44, 0x44,
                ],
            },
            port: DEFAULT_DNS_PORT,
            zone_index: 3,
        })),
        source: Some(fname::DnsServerSource::Dhcpv6(fname::Dhcpv6DnsServerSource {
            source_interface: Some(3),
        })),
    };
}
