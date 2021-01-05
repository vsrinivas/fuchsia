// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address, Subnet};
use fidl_fuchsia_net_interfaces::Address;
use fidl_fuchsia_net_stack::{AdministrativeStatus, InterfaceInfo, PhysicalStatus};
use serde::{Deserialize, Serialize};
/// Enum for supported FIDL commands.
pub enum NetstackMethod {
    DisableInterface,
    EnableInterface,
    GetInterfaceInfo,
    GetIpv6Addresses,
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
            "GetIpv6Addresses" => NetstackMethod::GetIpv6Addresses,
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

#[derive(Serialize, Deserialize)]
pub struct AddressDto {
    pub addr: Option<SubnetDto>,
}

#[derive(Serialize, Deserialize)]
pub struct SubnetDto {
    pub addr: IpAddressDto,
    pub prefix_len: u8,
}

#[derive(Serialize, Deserialize)]
pub enum IpAddressDto {
    Ipv4(Ipv4AddressDto),
    Ipv6(Ipv6AddressDto),
}

#[derive(Serialize, Deserialize)]
pub struct Ipv4AddressDto {
    pub addr: [u8; 4],
}

#[derive(Serialize, Deserialize)]
pub struct Ipv6AddressDto {
    pub addr: [u8; 16],
}

impl Into<AddressDto> for Address {
    fn into(self) -> AddressDto {
        AddressDto {
            addr: match self.addr {
                Some(subnet) => Some(subnet.into()),
                None => None,
            },
        }
    }
}

impl Into<SubnetDto> for Subnet {
    fn into(self) -> SubnetDto {
        SubnetDto { addr: self.addr.into(), prefix_len: self.prefix_len }
    }
}

impl Into<IpAddressDto> for IpAddress {
    fn into(self) -> IpAddressDto {
        match self {
            IpAddress::Ipv4(ipv4) => IpAddressDto::Ipv4(Ipv4AddressDto { addr: ipv4.addr }),
            IpAddress::Ipv6(ipv6) => IpAddressDto::Ipv6(Ipv6AddressDto { addr: ipv6.addr }),
        }
    }
}

impl Into<Address> for AddressDto {
    fn into(self) -> Address {
        Address {
            addr: match self.addr {
                Some(subnet) => Some(subnet.into()),
                None => None,
            },
            ..Address::EMPTY
        }
    }
}

impl Into<Subnet> for SubnetDto {
    fn into(self) -> Subnet {
        Subnet { addr: self.addr.into(), prefix_len: self.prefix_len }
    }
}

impl Into<IpAddress> for IpAddressDto {
    fn into(self) -> IpAddress {
        match self {
            IpAddressDto::Ipv4(ipv4) => IpAddress::Ipv4(Ipv4Address { addr: ipv4.addr }),
            IpAddressDto::Ipv6(ipv6) => IpAddress::Ipv6(Ipv6Address { addr: ipv6.addr }),
        }
    }
}
