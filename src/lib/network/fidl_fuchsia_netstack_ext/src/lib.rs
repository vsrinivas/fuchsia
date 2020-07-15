// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_netstack as fidl;

pub struct RouteTableEntry2 {
    pub destination: fidl_fuchsia_net_ext::IpAddress,
    pub netmask: fidl_fuchsia_net_ext::IpAddress,
    pub gateway: Option<fidl_fuchsia_net_ext::IpAddress>,
    pub nicid: u32,
    pub metric: u32,
}

impl From<fidl::RouteTableEntry2> for RouteTableEntry2 {
    fn from(
        fidl::RouteTableEntry2 {
            destination, netmask, gateway, nicid, metric
            }: fidl::RouteTableEntry2,
    ) -> Self {
        let destination = destination.into();
        let netmask = netmask.into();
        let gateway = gateway.map(|gateway| (*gateway).into());

        Self { destination, netmask, gateway, nicid, metric }
    }
}
