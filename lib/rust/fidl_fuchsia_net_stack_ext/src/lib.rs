// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_stack as fidl;

use bitflags::bitflags;

pub struct InterfaceAddress {
    ip_address: fidl_fuchsia_net_ext::IpAddress,
    prefix_len: u8,
    #[allow(unused)]
    peer_address: Option<Box<fidl_fuchsia_net::IpAddress>>,
}

impl From<fidl::InterfaceAddress> for InterfaceAddress {
    fn from(interface_address: fidl::InterfaceAddress) -> Self {
        let fidl::InterfaceAddress {
            ip_address,
            prefix_len,
            peer_address,
        } = interface_address;
        let ip_address = ip_address.into();
        Self {
            ip_address,
            prefix_len,
            peer_address,
        }
    }
}

impl std::fmt::Display for InterfaceAddress {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let Self {
            ip_address,
            prefix_len,
            peer_address: _,
        } = self;
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
        let fidl::ForwardingEntry {
            subnet,
            destination,
        } = forwarding_entry;
        let subnet = subnet.into();
        let destination = destination.into();
        Self {
            subnet,
            destination,
        }
    }
}

impl std::fmt::Display for ForwardingEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let Self {
            subnet,
            destination,
        } = self;
        write!(f, "{}", subnet)?;
        write!(f, "{}", destination)?;
        Ok(())
    }
}

bitflags! {
    /// Status flags for an interface.
    #[repr(transparent)]
    pub struct InterfaceStatus: u32 {
        const ENABLED = fidl::INTERFACE_STATUS_ENABLED;
        const LINK_UP = fidl::INTERFACE_STATUS_LINK_UP;
    }
}

pub struct InterfaceInfo {
    id: u64,
    path: String,
    mac: Option<fidl_zircon_ethernet_ext::MacAddress>,
    mtu: u32,
    features: fidl_zircon_ethernet_ext::EthernetFeatures,
    status: InterfaceStatus,
    addresses: Vec<InterfaceAddress>,
}

impl From<fidl::InterfaceInfo> for InterfaceInfo {
    fn from(
        fidl::InterfaceInfo {
            id,
            path,
            mac,
            mtu,
            features,
            status,
            addresses,
        }: fidl::InterfaceInfo,
    ) -> Self {
        let mac = mac.map(|mac| (*mac).into());
        let features = fidl_zircon_ethernet_ext::EthernetFeatures::from_bits_truncate(features);
        let status = InterfaceStatus::from_bits_truncate(status);
        let addresses = addresses.into_iter().map(Into::into).collect();
        Self {
            id,
            path,
            mac,
            mtu,
            features,
            status,
            addresses,
        }
    }
}

impl std::fmt::Display for InterfaceInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let InterfaceInfo {
            id,
            path,
            mac,
            mtu,
            features,
            status,
            addresses,
        } = self;
        write!(f, "Interface Info\n")?;
        write!(f, "  id: {}\n", id)?;
        write!(f, "  path: {}\n", path)?;
        if let Some(mac) = mac {
            write!(f, "  mac: {}\n", mac)?;
        }
        write!(f, "  mtu: {}\n", mtu)?;
        write!(f, "  features:\n    {:?}\n", features)?;
        write!(f, "  status:\n    {:?}\n", status)?;
        write!(f, "  Addresses:")?;
        for address in addresses {
            write!(f, "\n    {}", address)?;
        }
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
                path: "/all/the/way/home".to_owned(),
                mac: Some(fidl_zircon_ethernet_ext::MacAddress {
                    octets: [0, 1, 2, 255, 254, 253]
                }),
                mtu: 1500,
                features: fidl_zircon_ethernet_ext::EthernetFeatures::all(),
                status: InterfaceStatus::all(),
                addresses: vec![InterfaceAddress {
                    ip_address: fidl_fuchsia_net_ext::IpAddress(std::net::IpAddr::V4(
                        std::net::Ipv4Addr::new(255, 255, 255, 0),
                    ),),
                    prefix_len: 4,
                    peer_address: None,
                }],
            }
        ),
        r#"Interface Info
  id: 1
  path: /all/the/way/home
  mac: 00:01:02:ff:fe:fd
  mtu: 1500
  features:
    WLAN | SYNTHETIC | LOOPBACK
  status:
    ENABLED | LINK_UP
  Addresses:
    255.255.255.0/4"#
    );
}
