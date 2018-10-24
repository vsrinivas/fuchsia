// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fidl;

pub struct IpAddress<'a>(pub &'a fidl::IpAddress);

impl<'a> std::fmt::Display for IpAddress<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let IpAddress(ip_address) = self;
        match ip_address {
            fidl::IpAddress::Ipv4(a) => write!(
                f,
                "{}",
                ::std::net::IpAddr::V4(::std::net::Ipv4Addr::from(a.addr))
            ),
            fidl::IpAddress::Ipv6(a) => write!(
                f,
                "{}",
                ::std::net::IpAddr::V6(::std::net::Ipv6Addr::from(a.addr))
            ),
        }
    }
}
