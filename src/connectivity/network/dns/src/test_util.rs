// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as net;
use fidl_fuchsia_net_name as name;
use net_declare::fidl_socket_addr;

pub(crate) fn get_server_address(srv: net::SocketAddress) -> net::IpAddress {
    match srv {
        net::SocketAddress::Ipv4(addr) => net::IpAddress::Ipv4(addr.address),
        net::SocketAddress::Ipv6(addr) => net::IpAddress::Ipv6(addr.address),
    }
}

pub(crate) const DEFAULT_SERVER_A: net::SocketAddress = fidl_socket_addr!(8.8.8.8:53);
pub(crate) const DEFAULT_SERVER_B: net::SocketAddress =
    fidl_socket_addr!([2001:4860:4860::8888]:53);
pub(crate) const DYNAMIC_SERVER_A: net::SocketAddress = fidl_socket_addr!(8.8.4.4:53);
pub(crate) const DYNAMIC_SERVER_B: net::SocketAddress =
    fidl_socket_addr!([2001:4860:4860::4444]:53);

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
