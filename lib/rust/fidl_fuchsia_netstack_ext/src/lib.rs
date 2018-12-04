// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_netstack as fidl;

#[derive(Debug, Eq, PartialEq, Clone, Copy)]
pub enum IpAddressConfig {
    StaticIp(fidl_fuchsia_net_ext::Subnet),
    Dhcp,
}

impl Into<fidl::IpAddressConfig> for IpAddressConfig {
    fn into(self) -> fidl::IpAddressConfig {
        match self {
            IpAddressConfig::Dhcp => fidl::IpAddressConfig::Dhcp(false),
            IpAddressConfig::StaticIp(subnet) => fidl::IpAddressConfig::StaticIp(subnet.into()),
        }
    }
}
