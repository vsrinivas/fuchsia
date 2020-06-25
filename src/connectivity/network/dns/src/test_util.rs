// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as net;

use crate::DEFAULT_PORT;

pub const STATIC_SERVER: net::SocketAddress = net::SocketAddress::Ipv4(net::Ipv4SocketAddress {
    address: net::Ipv4Address { addr: [8, 8, 8, 8] },
    port: DEFAULT_PORT,
});

pub const DHCP_SERVER: net::SocketAddress = net::SocketAddress::Ipv4(net::Ipv4SocketAddress {
    address: net::Ipv4Address { addr: [8, 8, 4, 4] },
    port: DEFAULT_PORT,
});

pub const NDP_SERVER: net::SocketAddress = net::SocketAddress::Ipv6(net::Ipv6SocketAddress {
    address: net::Ipv6Address {
        addr: [
            0x20, 0x01, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x44, 0x44,
        ],
    },
    port: DEFAULT_PORT,
    zone_index: 2,
});

pub const DHCPV6_SERVER: net::SocketAddress = net::SocketAddress::Ipv6(net::Ipv6SocketAddress {
    address: net::Ipv6Address {
        addr: [
            0x20, 0x02, 0x48, 0x60, 0x48, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x44, 0x44,
        ],
    },
    port: DEFAULT_PORT,
    zone_index: 3,
});
