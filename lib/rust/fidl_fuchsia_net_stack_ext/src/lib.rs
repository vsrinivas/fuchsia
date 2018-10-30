// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_stack as fidl;

use bitflags::bitflags;

pub struct InterfaceAddress<'a>(&'a fidl::InterfaceAddress);

impl<'a> From<&'a fidl::InterfaceAddress> for InterfaceAddress<'a> {
    fn from(a: &'a fidl::InterfaceAddress) -> Self {
        InterfaceAddress(a)
    }
}

impl<'a> std::fmt::Display for InterfaceAddress<'a> {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let InterfaceAddress(fidl::InterfaceAddress {
            ip_address,
            prefix_len,
            peer_address: _,
        }) = self;
        write!(
            f,
            "{}/{}",
            fidl_fuchsia_net_ext::IpAddress(ip_address),
            prefix_len
        )
    }
}

pub struct ForwardingEntry(fidl::ForwardingEntry);

impl From<fidl::ForwardingEntry> for ForwardingEntry {
    fn from(e: fidl::ForwardingEntry) -> Self {
        ForwardingEntry(e)
    }
}

impl std::fmt::Display for ForwardingEntry {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let ForwardingEntry(fidl::ForwardingEntry {
            subnet,
            destination,
        }) = self;
        write!(
            f,
            "{}/{}: ",
            fidl_fuchsia_net_ext::IpAddress(&subnet.addr),
            subnet.prefix_len
        )?;
        match destination {
            fidl::ForwardingDestination::DeviceId(id) => write!(f, "device id {}", id),
            fidl::ForwardingDestination::NextHop(ref nh) => {
                write!(f, "next hop {}", fidl_fuchsia_net_ext::IpAddress(&nh))
            }
        }
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
    addresses: Vec<fidl::InterfaceAddress>,
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
        for addr in addresses {
            write!(f, "\n    {}", InterfaceAddress(addr))?;
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
                addresses: vec![fidl_fuchsia_net_stack::InterfaceAddress {
                    ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::IPv4Address {
                        addr: [255, 255, 255, 0]
                    }),
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
