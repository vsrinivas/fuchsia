// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Address, Properties, Update, UpdateResult, WatcherOperationError};

use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_interfaces as fnet_interfaces;
use futures::{Stream, TryStreamExt};
use net_types::{LinkLocalAddress as _, ScopeableAddress as _};
use std::collections::{HashMap, HashSet};
use thiserror::Error;

/// Returns true iff the supplied [`Properties`] (expected to be fully populated)
/// appears to provide network connectivity, i.e. is not loopback, is online, and has a default
/// route and a globally routable address for either IPv4 or IPv6. An IPv4 address is assumed to be
/// globally routable if it's not link-local. An IPv6 address is assumed to be globally routable if
/// it has global scope.
pub fn is_globally_routable(
    &Properties {
        ref device_class,
        online,
        ref addresses,
        has_default_ipv4_route,
        has_default_ipv6_route,
        ..
    }: &Properties,
) -> bool {
    match device_class {
        fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {}) => {
            return false;
        }
        fnet_interfaces::DeviceClass::Device(device) => match device {
            fidl_fuchsia_hardware_network::DeviceClass::Virtual
            | fidl_fuchsia_hardware_network::DeviceClass::Ethernet
            | fidl_fuchsia_hardware_network::DeviceClass::Wlan
            | fidl_fuchsia_hardware_network::DeviceClass::WlanAp
            | fidl_fuchsia_hardware_network::DeviceClass::Ppp
            | fidl_fuchsia_hardware_network::DeviceClass::Bridge => {}
        },
    }
    if !online {
        return false;
    }
    if !has_default_ipv4_route && !has_default_ipv6_route {
        return false;
    }
    addresses.iter().any(
        |Address { addr: fnet::Subnet { addr, prefix_len: _ }, valid_until: _ }| match addr {
            fnet::IpAddress::Ipv4(fnet::Ipv4Address { addr }) => {
                has_default_ipv4_route && !net_types::ip::Ipv4Addr::new(*addr).is_link_local()
            }
            fnet::IpAddress::Ipv6(fnet::Ipv6Address { addr }) => {
                has_default_ipv6_route
                    && net_types::ip::Ipv6Addr::from_bytes(*addr).scope()
                        == net_types::ip::Ipv6Scope::Global
            }
        },
    )
}

/// Wraps `event_stream` and returns a stream which yields the reachability status as a bool (true
/// iff there exists an interface with properties that satisfy [`is_globally_routable`]) whenever
/// it changes. The first item the returned stream yields is the reachability status of the first
/// interface discovered through an `Added` or `Existing` event on `event_stream`.
///
/// Note that `event_stream` must be created from a watcher with interest in all
/// fields, such as one created from [`crate::event_stream_from_state`].
pub fn to_reachability_stream(
    event_stream: impl Stream<Item = Result<fnet_interfaces::Event, fidl::Error>>,
) -> impl Stream<Item = Result<bool, WatcherOperationError<HashMap<u64, Properties>>>> {
    let mut if_map = HashMap::new();
    let mut reachable = None;
    let mut reachable_ids = HashSet::new();
    event_stream.map_err(WatcherOperationError::EventStream).try_filter_map(move |event| {
        futures::future::ready(if_map.update(event).map_err(WatcherOperationError::Update).map(
            |changed| {
                let reachable_ids_changed = match changed {
                    UpdateResult::Existing(properties)
                    | UpdateResult::Added(properties)
                    | UpdateResult::Changed { previous: _, current: properties }
                        if is_globally_routable(properties) =>
                    {
                        reachable_ids.insert(properties.id)
                    }
                    UpdateResult::Existing(_) | UpdateResult::Added(_) => false,
                    UpdateResult::Changed { previous: _, current: properties } => {
                        reachable_ids.remove(&properties.id)
                    }
                    UpdateResult::Removed(properties) => reachable_ids.remove(&properties.id),
                    UpdateResult::NoChange => return None,
                };
                // If the stream hasn't yielded anything yet, do so even if the set of reachable
                // interfaces hasn't changed.
                if reachable.is_none() {
                    reachable = Some(!reachable_ids.is_empty());
                    return reachable;
                } else if reachable_ids_changed {
                    let new_reachable = Some(!reachable_ids.is_empty());
                    if reachable != new_reachable {
                        reachable = new_reachable;
                        return reachable;
                    }
                }
                None
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
) -> Result<(), OperationError<HashMap<u64, Properties>>> {
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
    use fuchsia_zircon_types as zx;
    use futures::FutureExt as _;
    use net_declare::fidl_subnet;
    use std::convert::TryInto as _;

    const IPV4_LINK_LOCAL: fnet::Subnet = fidl_subnet!("169.254.0.1/16");
    const IPV6_LINK_LOCAL: fnet::Subnet = fidl_subnet!("fe80::1/64");
    const IPV4_GLOBAL: fnet::Subnet = fidl_subnet!("192.168.0.1/16");
    const IPV6_GLOBAL: fnet::Subnet = fidl_subnet!("100::1/64");

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
                    valid_until: Some(zx::ZX_TIME_INFINITE),
                    ..fnet_interfaces::Address::EMPTY
                },
                fnet_interfaces::Address {
                    addr: Some(IPV4_LINK_LOCAL),
                    valid_until: Some(zx::ZX_TIME_INFINITE),
                    ..fnet_interfaces::Address::EMPTY
                },
                fnet_interfaces::Address {
                    addr: Some(IPV6_GLOBAL),
                    valid_until: Some(zx::ZX_TIME_INFINITE),
                    ..fnet_interfaces::Address::EMPTY
                },
                fnet_interfaces::Address {
                    addr: Some(IPV6_LINK_LOCAL),
                    valid_until: Some(zx::ZX_TIME_INFINITE),
                    ..fnet_interfaces::Address::EMPTY
                },
            ]),
            has_default_ipv4_route: Some(true),
            has_default_ipv6_route: Some(true),
            ..fnet_interfaces::Properties::EMPTY
        }
    }

    #[test]
    fn test_is_globally_routable() -> Result<(), anyhow::Error> {
        const ID: u64 = 1;
        // These combinations are not globally routable.
        assert!(!is_globally_routable(&Properties {
            device_class: fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {}),
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            online: false,
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            addresses: vec![],
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            has_default_ipv4_route: false,
            has_default_ipv6_route: false,
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            addresses: vec![Address { addr: IPV4_GLOBAL, valid_until: zx::ZX_TIME_INFINITE }],
            has_default_ipv4_route: false,
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            addresses: vec![Address { addr: IPV6_GLOBAL, valid_until: zx::ZX_TIME_INFINITE }],
            has_default_ipv6_route: false,
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            addresses: vec![Address { addr: IPV6_LINK_LOCAL, valid_until: zx::ZX_TIME_INFINITE }],
            has_default_ipv6_route: true,
            ..valid_interface(ID).try_into()?
        }));
        assert!(!is_globally_routable(&Properties {
            addresses: vec![Address { addr: IPV4_LINK_LOCAL, valid_until: zx::ZX_TIME_INFINITE }],
            has_default_ipv4_route: true,
            ..valid_interface(ID).try_into()?
        }));

        // These combinations are globally routable.
        assert!(is_globally_routable(&valid_interface(ID).try_into()?));
        assert!(is_globally_routable(&Properties {
            addresses: vec![Address { addr: IPV4_GLOBAL, valid_until: zx::ZX_TIME_INFINITE }],
            has_default_ipv4_route: true,
            has_default_ipv6_route: false,
            ..valid_interface(ID).try_into()?
        }));
        assert!(is_globally_routable(&Properties {
            addresses: vec![Address { addr: IPV6_GLOBAL, valid_until: zx::ZX_TIME_INFINITE }],
            has_default_ipv4_route: false,
            has_default_ipv6_route: true,
            ..valid_interface(ID).try_into()?
        }));
        Ok(())
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
