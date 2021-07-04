// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_ext as fnet_ext;
use fidl_fuchsia_netstack as fidl;
use prettytable::{cell, row, Row, Table};
use std::io::Result;

pub struct RouteTableEntry {
    pub destination: fnet_ext::Subnet,
    pub gateway: Option<fnet_ext::IpAddress>,
    pub nicid: u32,
    pub metric: u32,
}

impl From<fidl::RouteTableEntry> for RouteTableEntry {
    fn from(
        fidl::RouteTableEntry { destination, gateway, nicid, metric }: fidl::RouteTableEntry,
    ) -> Self {
        let destination = destination.into();
        let gateway = gateway.map(|gateway| (*gateway).into());

        Self { destination, gateway, nicid, metric }
    }
}

pub struct RouteTable(Vec<fidl::RouteTableEntry>);

impl RouteTable {
    pub fn new(entries: Vec<fidl::RouteTableEntry>) -> Self {
        Self(entries)
    }

    pub fn display(&self) -> Result<String> {
        let RouteTable(route_table) = self;

        let mut table = Table::new();
        let _: &mut Row =
            table.add_row(row!["Destination", "Netmask", "Gateway", "NIC ID", "Metric"]);
        for fidl::RouteTableEntry { destination, gateway, nicid, metric } in route_table {
            let _: &mut Row = table.add_row(row![
                fnet_ext::Subnet::from(*destination),
                match gateway {
                    Some(gateway) => fnet_ext::IpAddress::from(**gateway).to_string(),
                    None => "-".to_string(),
                },
                nicid,
                metric,
            ]);
        }
        let mut bytes = Vec::new();
        let _lines_printed: usize = table.print(&mut bytes)?;
        Ok(String::from_utf8(bytes).unwrap())
    }
}
