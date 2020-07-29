// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_ext::IpAddress;
use fidl_fuchsia_netstack as fidl;
use prettytable::{cell, row, Table};
use std::io::Result;

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

pub struct RouteTable(Vec<fidl::RouteTableEntry>);

impl RouteTable {
    pub fn new(entries: Vec<fidl::RouteTableEntry>) -> Self {
        Self(entries)
    }

    pub fn display(&self) -> Result<String> {
        let mut table = Table::new();
        table.add_row(row!["Destination", "Netmask", "Gateway", "NicID"]);
        let RouteTable(route_table) = self;
        for fidl::RouteTableEntry { destination, netmask, gateway, nicid } in route_table.iter() {
            table.add_row(row![
                IpAddress::from(*destination).to_string(),
                IpAddress::from(*netmask).to_string(),
                IpAddress::from(*gateway).to_string(),
                nicid.to_string(),
            ]);
        }
        let mut bytes = Vec::new();
        table.print(&mut bytes)?;
        Ok(String::from_utf8(bytes).unwrap())
    }
}
