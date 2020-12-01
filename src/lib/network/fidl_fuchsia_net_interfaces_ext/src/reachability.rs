// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Update, UpdateResult, WatcherOperationError};

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use futures::{Stream, TryStreamExt};
use net_types::{LinkLocalAddress as _, ScopeableAddress as _};
use std::collections::{HashMap, HashSet};
use thiserror::Error;

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

/// Wraps `event_stream` and returns a stream which yields the reachability status as a bool (true
/// iff there exists an interface with properties that satisfy [`is_globally_routable`]) whenever
/// it changes.
pub fn to_reachability_stream(
    event_stream: impl Stream<Item = Result<fnet_interfaces::Event, fidl::Error>>,
) -> impl Stream<Item = Result<bool, WatcherOperationError<HashMap<u64, fnet_interfaces::Properties>>>>
{
    let mut if_map = HashMap::new();
    let mut reachable = None;
    let mut reachable_ids = HashSet::new();
    event_stream.map_err(WatcherOperationError::EventStream).try_filter_map(move |event| {
        futures::future::ready(if_map.update(event).map_err(WatcherOperationError::Update).map(
            |changed| {
                match changed {
                    UpdateResult::Existing(properties)
                    | UpdateResult::Added(properties)
                    | UpdateResult::Changed(properties) => {
                        if let Some(id) = properties.id {
                            if is_globally_routable(properties) {
                                let _present: bool = reachable_ids.insert(id);
                            } else {
                                let _removed: bool = reachable_ids.remove(&id);
                            }
                        }
                    }
                    UpdateResult::Removed(id) => {
                        let _removed: bool = reachable_ids.remove(&id);
                    }
                    UpdateResult::NoChange => {
                        return None;
                    }
                }
                let new_reachable = Some(!reachable_ids.is_empty());
                if reachable != new_reachable {
                    reachable = new_reachable;
                    reachable
                } else {
                    None
                }
            },
        ))
    })
}

/// Reachability status stream operational errors.
#[derive(Error, Debug)]
pub enum OperationError<B: Update + std::fmt::Debug> {
    #[error("watcher operation error: {0}")]
    Watcher(WatcherOperationError<B>),
    #[error("reachability status stream ended unexpectedly")]
    UnexpectedEnd,
}

/// Returns a future which resolves when any network interface observed through `event_stream`
/// has properties which satisfy [`is_globally_routable`].
pub async fn wait_for_reachability(
    event_stream: impl Stream<Item = Result<fnet_interfaces::Event, fidl::Error>>,
) -> Result<(), OperationError<HashMap<u64, fnet_interfaces::Properties>>> {
    futures::pin_mut!(event_stream);
    let rtn = to_reachability_stream(event_stream)
        .map_err(OperationError::Watcher)
        .try_filter_map(|reachable| futures::future::ok(if reachable { Some(()) } else { None }))
        .try_next()
        .await
        .and_then(|item| item.ok_or_else(|| OperationError::UnexpectedEnd));
    rtn
}

#[cfg(test)]
mod tests {
    use super::*;

    use anyhow::Context as _;
    use fidl_fuchsia_hardware_network as fnetwork;
    use futures::FutureExt as _;
    use net_declare::fidl_subnet;

    const IPV4_LINK_LOCAL: fnet::Subnet = fidl_subnet!(169.254.0.1/16);
    const IPV6_LINK_LOCAL: fnet::Subnet = fidl_subnet!(fe80::1/64);
    const IPV4_GLOBAL: fnet::Subnet = fidl_subnet!(192.168.0.1/16);
    const IPV6_GLOBAL: fnet::Subnet = fidl_subnet!(100::1/64);

    fn valid_interface(id: u64) -> fnet_interfaces::Properties {
        fnet_interfaces::Properties {
            id: Some(id),
            name: Some("test1".to_string()),
            device_class: Some(fnet_interfaces::DeviceClass::Device(
                fnetwork::DeviceClass::Ethernet,
            )),
            online: Some(true),
            addresses: Some(vec![
                fnet_interfaces::Address {
                    addr: Some(IPV4_GLOBAL),
                    ..fnet_interfaces::Address::EMPTY
                },
                fnet_interfaces::Address {
                    addr: Some(IPV4_LINK_LOCAL),
                    ..fnet_interfaces::Address::EMPTY
                },
                fnet_interfaces::Address {
                    addr: Some(IPV6_GLOBAL),
                    ..fnet_interfaces::Address::EMPTY
                },
                fnet_interfaces::Address {
                    addr: Some(IPV6_LINK_LOCAL),
                    ..fnet_interfaces::Address::EMPTY
                },
            ]),
            has_default_ipv4_route: Some(true),
            has_default_ipv6_route: Some(true),
            ..fnet_interfaces::Properties::EMPTY
        }
    }

    #[test]
    fn test_is_globally_routable() {
        const ID: u64 = 1;
        // These combinations are not globally routable.
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            device_class: None,
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            device_class: Some(fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {})),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            online: Some(false),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![]),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(false),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV4_GLOBAL),
                ..fnet_interfaces::Address::EMPTY
            }]),
            has_default_ipv4_route: Some(false),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV6_GLOBAL),
                ..fnet_interfaces::Address::EMPTY
            }]),
            has_default_ipv6_route: Some(false),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV6_LINK_LOCAL),
                ..fnet_interfaces::Address::EMPTY
            }]),
            has_default_ipv6_route: Some(true),
            ..valid_interface(ID)
        }));
        assert!(!is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV4_LINK_LOCAL),
                ..fnet_interfaces::Address::EMPTY
            }]),
            has_default_ipv4_route: Some(true),
            ..valid_interface(ID)
        }));

        // These combinations are globally routable.
        assert!(is_globally_routable(&valid_interface(ID)));
        assert!(is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV4_GLOBAL),
                ..fnet_interfaces::Address::EMPTY
            }]),
            has_default_ipv4_route: Some(true),
            has_default_ipv6_route: Some(false),
            ..valid_interface(ID)
        }));
        assert!(is_globally_routable(&fnet_interfaces::Properties {
            addresses: Some(vec![fnet_interfaces::Address {
                addr: Some(IPV6_GLOBAL),
                ..fnet_interfaces::Address::EMPTY
            }]),
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(true),
            ..valid_interface(ID)
        }));
    }

    #[test]
    fn test_to_reachability_stream() -> Result<(), anyhow::Error> {
        let (sender, receiver) = futures::channel::mpsc::unbounded();
        let mut reachability_stream = to_reachability_stream(receiver);
        for (event, want) in vec![
            (fnet_interfaces::Event::Idle(fnet_interfaces::Empty {}), None),
            // Added events
            (
                fnet_interfaces::Event::Added(fnet_interfaces::Properties {
                    online: Some(false),
                    ..valid_interface(1)
                }),
                Some(false),
            ),
            (fnet_interfaces::Event::Added(valid_interface(2)), Some(true)),
            (
                fnet_interfaces::Event::Added(fnet_interfaces::Properties {
                    online: Some(false),
                    ..valid_interface(3)
                }),
                None,
            ),
            // Changed events
            (
                fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
                    id: Some(2),
                    online: Some(false),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                Some(false),
            ),
            (
                fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
                    id: Some(1),
                    online: Some(true),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                Some(true),
            ),
            (
                fnet_interfaces::Event::Changed(fnet_interfaces::Properties {
                    id: Some(3),
                    online: Some(true),
                    ..fnet_interfaces::Properties::EMPTY
                }),
                None,
            ),
            // Removed events
            (fnet_interfaces::Event::Removed(1), None),
            (fnet_interfaces::Event::Removed(3), Some(false)),
            (fnet_interfaces::Event::Removed(2), None),
        ] {
            let () = sender.unbounded_send(Ok(event.clone())).context("failed to send event")?;
            let got = reachability_stream.try_next().now_or_never();
            if let Some(want_reachable) = want {
                let r = got.ok_or_else(|| {
                    anyhow::anyhow!("reachability status stream unexpectedly yielded nothing")
                })?;
                let item = r.context("reachability status stream error")?;
                let got_reachable = item.ok_or_else(|| {
                    anyhow::anyhow!("reachability status stream ended unexpectedly")
                })?;
                assert_eq!(got_reachable, want_reachable);
            } else {
                if got.is_some() {
                    panic!("got {:?} from reachability stream after event {:?}, want None as reachability status should not have changed", got, event);
                }
            }
        }
        Ok(())
    }
}
