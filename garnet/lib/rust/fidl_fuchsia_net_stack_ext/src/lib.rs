// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_stack as fidl;

pub struct InterfaceAddress {
    ip_address: fidl_fuchsia_net_ext::IpAddress,
    prefix_len: u8,
}

impl From<fidl::InterfaceAddress> for InterfaceAddress {
    fn from(interface_address: fidl::InterfaceAddress) -> Self {
        let fidl::InterfaceAddress { ip_address, prefix_len } = interface_address;
        let ip_address = ip_address.into();
        Self { ip_address, prefix_len }
    }
}

impl std::fmt::Display for InterfaceAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let Self { ip_address, prefix_len } = self;
        write!(f, "{}/{}", ip_address, prefix_len)
    }
}

pub enum ForwardingDestination {
    DeviceId(u64),
    NextHop(fidl_fuchsia_net_ext::IpAddress),
}

impl From<fidl::ForwardingDestination> for ForwardingDestination {
    fn from(forwarding_destination: fidl::ForwardingDestination) -> Self {
        match forwarding_destination {
            fidl::ForwardingDestination::DeviceId(id) => ForwardingDestination::DeviceId(id),
            fidl::ForwardingDestination::NextHop(ip_address) => {
                ForwardingDestination::NextHop(ip_address.into())
            }
        }
    }
}

impl std::fmt::Display for ForwardingDestination {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        match self {
            ForwardingDestination::DeviceId(id) => write!(f, "device id {}", id),
            ForwardingDestination::NextHop(nh) => write!(f, "next hop {}", nh),
        }
    }
}

pub struct ForwardingEntry {
    subnet: fidl_fuchsia_net_ext::Subnet,
    destination: ForwardingDestination,
}

impl From<fidl::ForwardingEntry> for ForwardingEntry {
    fn from(forwarding_entry: fidl::ForwardingEntry) -> Self {
        let fidl::ForwardingEntry { subnet, destination } = forwarding_entry;
        let subnet = subnet.into();
        let destination = destination.into();
        Self { subnet, destination }
    }
}

impl std::fmt::Display for ForwardingEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let Self { subnet, destination } = self;
        write!(f, "{}", subnet)?;
        write!(f, "{}", destination)?;
        Ok(())
    }
}

pub enum AdministrativeStatus {
    DISABLED,
    ENABLED,
}

impl From<fidl::AdministrativeStatus> for AdministrativeStatus {
    fn from(administrative_status: fidl::AdministrativeStatus) -> Self {
        match administrative_status {
            fidl::AdministrativeStatus::Disabled => AdministrativeStatus::DISABLED,
            fidl::AdministrativeStatus::Enabled => AdministrativeStatus::ENABLED,
        }
    }
}

impl std::fmt::Display for AdministrativeStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        match self {
            AdministrativeStatus::DISABLED => write!(f, "DISABLED"),
            AdministrativeStatus::ENABLED => write!(f, "ENABLED"),
        }
    }
}

pub enum PhysicalStatus {
    UP,
    DOWN,
}

impl From<fidl::PhysicalStatus> for PhysicalStatus {
    fn from(physical_status: fidl::PhysicalStatus) -> Self {
        match physical_status {
            fidl::PhysicalStatus::Down => PhysicalStatus::DOWN,
            fidl::PhysicalStatus::Up => PhysicalStatus::UP,
        }
    }
}

impl std::fmt::Display for PhysicalStatus {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        match self {
            PhysicalStatus::DOWN => write!(f, "LINK_DOWN"),
            PhysicalStatus::UP => write!(f, "LINK_UP"),
        }
    }
}

pub struct InterfaceProperties {
    path: String,
    mac: Option<fidl_fuchsia_hardware_ethernet_ext::MacAddress>,
    mtu: u32,
    features: fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures,
    administrative_status: AdministrativeStatus,
    physical_status: PhysicalStatus,
    addresses: Vec<InterfaceAddress>,
}

impl From<fidl::InterfaceProperties> for InterfaceProperties {
    fn from(
        fidl::InterfaceProperties {
            path,
            mac,
            mtu,
            features,
            administrative_status,
            physical_status,
            addresses,
        }: fidl::InterfaceProperties,
    ) -> Self {
        let mac = mac.map(|mac| (*mac).into());
        let features =
            fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::from_bits_truncate(features);
        let administrative_status = AdministrativeStatus::from(administrative_status);
        let physical_status = PhysicalStatus::from(physical_status);
        let addresses = addresses.into_iter().map(Into::into).collect();
        Self { path, mac, mtu, features, administrative_status, physical_status, addresses }
    }
}

impl std::fmt::Display for InterfaceProperties {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let InterfaceProperties {
            path,
            mac,
            mtu,
            features,
            administrative_status,
            physical_status,
            addresses,
        } = self;
        write!(f, "  path: {}\n", path)?;
        if let Some(mac) = mac {
            write!(f, "  mac: {}\n", mac)?;
        }
        write!(f, "  mtu: {}\n", mtu)?;
        write!(f, "  features: {:?}\n", features)?;
        write!(f, "  status: {} | {}\n", administrative_status, physical_status)?;
        write!(f, "  Addresses:")?;
        for address in addresses {
            write!(f, "\n    {}", address)?;
        }
        Ok(())
    }
}
pub struct InterfaceInfo {
    id: u64,
    properties: InterfaceProperties,
}

impl From<fidl::InterfaceInfo> for InterfaceInfo {
    fn from(fidl::InterfaceInfo { id, properties }: fidl::InterfaceInfo) -> Self {
        let properties = InterfaceProperties::from(properties);
        Self { id, properties }
    }
}

impl std::fmt::Display for InterfaceInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let InterfaceInfo { id, properties } = self;
        write!(f, "Interface Info\n")?;
        write!(f, "  id: {}\n", id)?;
        write!(f, "{}", properties)?;
        Ok(())
    }
}

#[test]
fn test_display_interfaceinfo() {
    assert_eq!(
        &format!(
            "{}",
            InterfaceInfo {
                id: 1,
                properties: InterfaceProperties {
                    path: "/all/the/way/home".to_owned(),
                    mac: Some(fidl_fuchsia_hardware_ethernet_ext::MacAddress {
                        octets: [0, 1, 2, 255, 254, 253]
                    }),
                    mtu: 1500,
                    features: fidl_fuchsia_hardware_ethernet_ext::EthernetFeatures::all(),
                    administrative_status: AdministrativeStatus::ENABLED,
                    physical_status: PhysicalStatus::UP,
                    addresses: vec![
                        InterfaceAddress {
                            ip_address: fidl_fuchsia_net_ext::IpAddress(std::net::IpAddr::V4(
                                std::net::Ipv4Addr::new(255, 255, 255, 0),
                            ),),
                            prefix_len: 4,
                        },
                        InterfaceAddress {
                            ip_address: fidl_fuchsia_net_ext::IpAddress(std::net::IpAddr::V4(
                                std::net::Ipv4Addr::new(255, 255, 255, 1),
                            ),),
                            prefix_len: 4,
                        }
                    ],
                }
            }
        ),
        r#"Interface Info
  id: 1
  path: /all/the/way/home
  mac: 00:01:02:ff:fe:fd
  mtu: 1500
  features: WLAN | SYNTHETIC | LOOPBACK
  status: ENABLED | LINK_UP
  Addresses:
    255.255.255.0/4
    255.255.255.1/4"#
    );
}
