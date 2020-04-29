// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::DEFAULT_PORT;
use fidl_fuchsia_net as net;
use fidl_fuchsia_net_name as name;

pub(crate) const fn new_ipv6_socket_addr(
    addr: [u8; 16],
    port: u16,
    zone_index: u64,
) -> net::SocketAddress {
    net::SocketAddress::Ipv6(net::Ipv6SocketAddress {
        address: net::Ipv6Address { addr },
        port,
        zone_index,
    })
}

pub(crate) const fn new_ipv4_socket_addr(addr: [u8; 4], port: u16) -> net::SocketAddress {
    net::SocketAddress::Ipv4(net::Ipv4SocketAddress { address: net::Ipv4Address { addr }, port })
}

pub(crate) fn get_server_address(srv: net::SocketAddress) -> net::IpAddress {
    match srv {
        net::SocketAddress::Ipv4(addr) => net::IpAddress::Ipv4(addr.address),
        net::SocketAddress::Ipv6(addr) => net::IpAddress::Ipv6(addr.address),
    }
}

pub(crate) const DEFAULT_SERVER_A: net::SocketAddress =
    new_ipv4_socket_addr([8, 8, 8, 8], DEFAULT_PORT);
pub(crate) const DEFAULT_SERVER_B: net::SocketAddress = new_ipv6_socket_addr(
    [
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x88,
        0x88,
    ],
    DEFAULT_PORT,
    0,
);

pub(crate) const DYNAMIC_SERVER_A: net::SocketAddress =
    new_ipv4_socket_addr([8, 8, 4, 4], DEFAULT_PORT);
pub(crate) const DYNAMIC_SERVER_B: net::SocketAddress = new_ipv6_socket_addr(
    [
        0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x44,
        0x44,
    ],
    DEFAULT_PORT,
    0,
);

pub(crate) fn to_static_server(address: net::SocketAddress) -> name::DnsServer_ {
    name::DnsServer_ {
        address: Some(address),
        source: Some(name::DnsServerSource::StaticSource(name::StaticDnsServerSource {})),
    }
}

pub(crate) fn to_discovered_server(address: net::SocketAddress) -> name::DnsServer_ {
    // Mock the source based on the address version.
    name::DnsServer_ {
        address: Some(address),
        source: Some(match &address {
            net::SocketAddress::Ipv4(_) => {
                name::DnsServerSource::Dhcp(name::DhcpDnsServerSource { source_interface: Some(1) })
            }
            net::SocketAddress::Ipv6(_) => {
                name::DnsServerSource::Ndp(name::NdpDnsServerSource { source_interface: Some(1) })
            }
        }),
    }
}
