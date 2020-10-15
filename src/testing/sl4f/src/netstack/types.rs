// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//use fidl_fuchsia_hardware_ethernet::MacAddress;
use fidl_fuchsia_net_stack::{AdministrativeStatus, InterfaceInfo, PhysicalStatus};
use serde::Serialize;
//use std::net::IpAddr;
use fidl_fuchsia_net::IpAddress;

/// Enum for supported FIDL commands.
pub enum NetstackMethod {
    DisableInterface,
    EnableInterface,
    GetInterfaceInfo,
    InitNetstack,
    ListInterfaces,
    NetstackUndefined,
}

impl NetstackMethod {
    pub fn from_str(method: &String) -> NetstackMethod {
        match method.as_ref() {
            "DisableInterface" => NetstackMethod::DisableInterface,
            "EnableInterface" => NetstackMethod::EnableInterface,
            "GetInterfaceInfo" => NetstackMethod::GetInterfaceInfo,
            "ListInterfaces" => NetstackMethod::ListInterfaces,
            "InitNetstack" => NetstackMethod::InitNetstack,
            _ => NetstackMethod::NetstackUndefined,
        }
    }
}

#[derive(Clone, Debug, Serialize)]
pub struct CustomInterfaceInfo {
    pub id: u64,
    pub name: String,
    pub topopath: String,
    pub filepath: String,
    pub mac: Option<[u8; 6]>,
    pub mtu: u32,
    pub features: u32,
    pub is_administrative_status_enabled: bool,
    pub is_physical_status_up: bool,
    pub ipv4_addresses: Vec<Vec<u8>>,
    pub ipv6_addresses: Vec<Vec<u8>>,
}

impl CustomInterfaceInfo {
    pub fn new(info: &InterfaceInfo) -> Self {
        let id = info.id;
        let mac = if let Some(v) = &info.properties.mac { Some(v.octets) } else { None };
        let is_administrative_status_enabled = match info.properties.administrative_status {
            AdministrativeStatus::Enabled => true,
            AdministrativeStatus::Disabled => false,
        };

        let is_physical_status_up = match info.properties.physical_status {
            PhysicalStatus::Up => true,
            PhysicalStatus::Down => false,
        };

        let mut ipv4_addresses = Vec::new();
        let mut ipv6_addresses = Vec::new();

        for address_info in &info.properties.addresses {
            match address_info.addr {
                IpAddress::Ipv4(ip) => ipv4_addresses.push(ip.addr.to_vec()),
                IpAddress::Ipv6(ip) => ipv6_addresses.push(ip.addr.to_vec()),
            };
        }

        CustomInterfaceInfo {
            id: id,
            name: info.properties.name.clone(),
            topopath: info.properties.topopath.clone(),
            filepath: info.properties.filepath.clone(),
            mac: mac,
            mtu: info.properties.mtu,
            features: info.properties.features.bits(),
            is_administrative_status_enabled: is_administrative_status_enabled,
            is_physical_status_up: is_physical_status_up,
            ipv4_addresses: ipv4_addresses,
            ipv6_addresses: ipv6_addresses,
        }
    }
}
