// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net_policy as fidl;

pub struct InterfaceInfo {
    name: String,
    properties: fidl_fuchsia_net_stack_ext::InterfaceProperties,
}

impl From<fidl::InterfaceInfo> for InterfaceInfo {
    fn from(interface_info: fidl::InterfaceInfo) -> Self {
        let fidl::InterfaceInfo { name, properties } = interface_info;
        let properties = properties.into();
        Self { name, properties }
    }
}

impl std::fmt::Display for InterfaceInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> Result<(), std::fmt::Error> {
        let Self { name, properties } = self;
        write!(f, "{}\n{}", name, properties)
    }
}
#[test]
fn test_display_interfaceinfo() {
    let info: InterfaceInfo = fidl::InterfaceInfo {
        name: "eth0".to_owned(),
        properties: fidl_fuchsia_net_stack::InterfaceProperties {
            path: "/all/the/way/home".to_owned(),
            mac: Some(Box::new(fidl_fuchsia_hardware_ethernet::MacAddress {
                octets: [0, 1, 2, 255, 254, 253],
            })),
            mtu: 1500,
            features: 2,
            administrative_status: fidl_fuchsia_net_stack::AdministrativeStatus::Enabled,
            physical_status: fidl_fuchsia_net_stack::PhysicalStatus::Up,
            addresses: vec![
                fidl_fuchsia_net_stack::InterfaceAddress {
                    ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: [255, 255, 255, 0],
                    }),
                    prefix_len: 4,
                },
                fidl_fuchsia_net_stack::InterfaceAddress {
                    ip_address: fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: [255, 255, 255, 1],
                    }),
                    prefix_len: 4,
                },
            ],
        },
    }
    .into();
    assert_eq!(
        &format!("{}", info),
        r#"eth0
  path: /all/the/way/home
  mac: 00:01:02:ff:fe:fd
  mtu: 1500
  features: SYNTHETIC
  status: ENABLED | LINK_UP
  Addresses:
    255.255.255.0/4
    255.255.255.1/4"#
    );
}
