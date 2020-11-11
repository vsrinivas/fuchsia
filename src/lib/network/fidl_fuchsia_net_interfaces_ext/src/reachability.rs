// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use futures::TryFutureExt as _;
use net_types::{LinkLocalAddress as _, ScopeableAddress as _};
use std::collections::HashMap;

/// Returns true iff the supplied [`fnet_interfaces::Properties`] (expected to be fully populated)
/// appears to provide network connectivity, i.e. is not loopback, is online, and has a default
/// route and a globally routable address for either IPv4 or IPv6. An IPv4 address is assumed to be
/// globally routable if it's not link-local. An IPv6 address is assumed to be globally routable if
/// it has global scope.
pub fn is_globally_routable(
    &fnet_interfaces::Properties {
        ref device_class,
        online,
        ref addresses,
        has_default_ipv4_route,
        has_default_ipv6_route,
        ..
    }: &fnet_interfaces::Properties,
) -> bool {
    match device_class {
        None | Some(fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {})) => {
            return false;
        }
        Some(fnet_interfaces::DeviceClass::Device(device)) => match device {
            fidl_fuchsia_hardware_network::DeviceClass::Unknown
            | fidl_fuchsia_hardware_network::DeviceClass::Ethernet
            | fidl_fuchsia_hardware_network::DeviceClass::Wlan
            | fidl_fuchsia_hardware_network::DeviceClass::Ppp
            | fidl_fuchsia_hardware_network::DeviceClass::Bridge => {}
        },
    }
    if online != Some(true) {
        return false;
    }
    let has_default_ipv4_route = has_default_ipv4_route.unwrap_or(false);
    let has_default_ipv6_route = has_default_ipv6_route.unwrap_or(false);
    if !has_default_ipv4_route && !has_default_ipv6_route {
        return false;
    }
    addresses.as_ref().map_or(false, |addresses| {
        addresses.iter().any(|fnet_interfaces::Address { addr, .. }| match addr {
            Some(fnet::Subnet {
                addr: fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }),
                prefix_len: _,
            }) => has_default_ipv4_route && !net_types::ip::Ipv4Addr::new(*addr).is_linklocal(),
            Some(fnet::Subnet {
                addr: fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }),
                prefix_len: _,
            }) => {
                has_default_ipv6_route
                    && net_types::ip::Ipv6Addr::new(*addr).scope()
                        == net_types::ip::Ipv6Scope::Global
            }
            None => false,
        })
    })
}

/// Returns a future which resolves when any network interface's properties satisfy
/// [`is_globally_routable`].
///
/// Network interface property events are consumed from a watcher created via `interface_state`.
pub fn wait_for_reachability(
    interface_state: &fnet_interfaces::StateProxy,
) -> impl futures::Future<Output = Result<(), anyhow::Error>> {
    futures::future::ready(crate::event_stream_from_state(interface_state)).and_then(
        |event_stream| async {
            crate::wait_interface(event_stream, &mut HashMap::new(), |if_map| {
                if if_map.values().any(is_globally_routable) {
                    Some(())
                } else {
                    None
                }
            })
            .await
        },
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_hardware_network as fnetwork;
    use net_declare::fidl_subnet;

    const IPV4_LINK_LOCAL: fnet::Subnet = fidl_subnet!(169.254.0.1/16);
    const IPV6_LINK_LOCAL: fnet::Subnet = fidl_subnet!(fe80::1/64);
    const IPV4_GLOBAL: fnet::Subnet = fidl_subnet!(192.168.0.1/16);
    const IPV6_GLOBAL: fnet::Subnet = fidl_subnet!(100::1/64);

    fn valid_interface() -> fnet_interfaces::Properties {
        fnet_interfaces::Properties {
            id: Some(1),
            name: Some("test1".to_string()),
            device_class: Some(fnet_interfaces::DeviceClass::Device(
                fnetwork::DeviceClass::Ethernet,
            )),
            online: Some(true),
            addresses: Some(vec![
                fnet_interfaces::Address {
                    addr: Some(IPV4_GLOBAL),
                    ..fnet_interfaces::Address::empty()
                },
                fnet_interfaces::Address {
                    addr: Some(IPV4_LINK_LOCAL),
                    ..fnet_interfaces::Address::empty()
                },
                fnet_interfaces::Address {
                    addr: Some(IPV6_GLOBAL),
                    ..fnet_interfaces::Address::empty()
                },
                fnet_interfaces::Address {
                    addr: Some(IPV6_LINK_LOCAL),
                    ..fnet_interfaces::Address::empty()
                },
            ]),
            has_default_ipv4_route: Some(true),
            has_default_ipv6_route: Some(true),
            ..fnet_interfaces::Properties::empty()
        }
    }

    #[test]
    fn test_is_globally_routable() {
        // These combinations are not globally routable.
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            device_class: None,
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            device_class: Some(fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {})),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            online: Some(false),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![]),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(false),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV4_GLOBAL),
                ..fnet_interfaces::Address::empty()
            }]),
            has_default_ipv4_route: Some(false),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV6_GLOBAL),
                ..fnet_interfaces::Address::empty()
            }]),
            has_default_ipv6_route: Some(false),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV6_LINK_LOCAL),
                ..fnet_interfaces::Address::empty()
            }]),
            has_default_ipv6_route: Some(true),
            ..valid_interface()
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV4_LINK_LOCAL),
                ..fnet_interfaces::Address::empty()
            }]),
            has_default_ipv4_route: Some(true),
            ..valid_interface()
        }));

        // These combinations are globally routable.
        assert!(is_globally_routable(&valid_interface()));
        assert!(is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV4_GLOBAL),
                ..fnet_interfaces::Address::empty()
            }]),
            has_default_ipv4_route: Some(true),
            has_default_ipv6_route: Some(false),
            ..valid_interface()
        }));
        assert!(is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV6_GLOBAL),
                ..fnet_interfaces::Address::empty()
            }]),
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(true),
            ..valid_interface()
        }));
    }
}
