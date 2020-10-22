// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Useful constants for tests.

/// IPv6 constants.
pub mod ipv6 {
    use net_types::ip as net_types_ip;

    /// An IPv6 prefix.
    ///
    /// 2001:f1f0:4060:1::/64
    pub const PREFIX: net_types_ip::Subnet<net_types_ip::Ipv6Addr> = unsafe {
        net_types_ip::Subnet::new_unchecked(
            net_types_ip::Ipv6Addr::new([
                0x20, 0x01, 0xf1, 0xf0, 0x40, 0x60, 0x00, 0x01, 0, 0, 0, 0, 0, 0, 0, 0,
            ]),
            64,
        )
    };

    /// A link-local IPv6 address.
    ///
    /// fe80::1
    pub const LINK_LOCAL_ADDR: net_types_ip::Ipv6Addr =
        net_types_ip::Ipv6Addr::new([0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]);
}

/// Ethernet constants.
pub mod eth {
    use net_types::ethernet::Mac;

    /// A MAC address.
    ///
    /// 02:00:00:00:00:01
    pub const MAC_ADDR: Mac = Mac::new([0x02, 0x00, 0x00, 0x00, 0x00, 0x01]);
}
