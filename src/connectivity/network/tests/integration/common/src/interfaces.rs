// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Provides utilities for using `fuchsia.net.interfaces` and
//! `fuchsia.net.interfaces.admin` in Netstack integration tests.

use super::Result;

use anyhow::Context as _;
use fuchsia_async::{DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;
use futures::future::{FusedFuture, Future, FutureExt as _, TryFutureExt as _};
use std::collections::{HashMap, HashSet};

/// Waits for an interface to come up with the specified address.
pub async fn wait_for_interface_up_and_address(
    state: &fidl_fuchsia_net_interfaces::StateProxy,
    id: u64,
    // TODO(https://fxbug.dev/89901): Migrate to `fidl_fuchsia_net::InterfaceAddress`.
    want_addr: &fidl_fuchsia_net::Subnet,
) {
    fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&state)
            .expect("failed to get interfaces event stream"),
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(id),
        |fidl_fuchsia_net_interfaces_ext::Properties { online, addresses, .. }| {
            if !online {
                return None;
            }

            // If configuring static addresses, make sure the addresses are
            // present (this ensures that DAD has resolved for IPv6 addresses).
            if !addresses.iter().any(
                |fidl_fuchsia_net_interfaces_ext::Address { addr, valid_until: _ }| {
                    addr == want_addr
                },
            ) {
                return None;
            }

            Some(())
        },
    )
    .await
    .expect("failed waiting for interface to be up and configured")
}

/// Waits for a non-loopback interface to come up with an ID not in `exclude_ids`.
///
/// Useful when waiting for an interface to be discovered and brought up by a
/// network manager.
///
/// Returns the interface's ID and name.
pub async fn wait_for_non_loopback_interface_up<
    F: Unpin + FusedFuture + Future<Output = Result<component_events::events::Stopped>>,
>(
    interface_state: &fidl_fuchsia_net_interfaces::StateProxy,
    mut wait_for_netmgr: &mut F,
    exclude_ids: Option<&HashSet<u64>>,
    timeout: zx::Duration,
) -> Result<(u64, String)> {
    let mut if_map = HashMap::new();
    let wait_for_interface = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(interface_state)?,
        &mut if_map,
        |if_map| {
            if_map.iter().find_map(
                |(
                    id,
                    fidl_fuchsia_net_interfaces_ext::Properties {
                        name, device_class, online, ..
                    },
                )| {
                    (*device_class
                        != fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                            fidl_fuchsia_net_interfaces::Empty {},
                        )
                        && *online
                        && exclude_ids.map_or(true, |ids| !ids.contains(id)))
                    .then(|| (*id, name.clone()))
                },
            )
        },
    )
    .map_err(anyhow::Error::from)
    .on_timeout(timeout.after_now(), || Err(anyhow::anyhow!("timed out")))
    .map(|r| r.context("failed to wait for non-loopback interface up"))
    .fuse();
    fuchsia_async::pin_mut!(wait_for_interface);
    futures::select! {
        wait_for_interface_res = wait_for_interface => {
            wait_for_interface_res
        }
        stopped_event = wait_for_netmgr => {
            Err(anyhow::anyhow!("the network manager unexpectedly stopped with event = {:?}", stopped_event))
        }
    }
}

/// Add an address, returning once the assignment state is `Assigned`.
pub async fn add_address_wait_assigned(
    control: &fidl_fuchsia_net_interfaces_ext::admin::Control,
    mut address: fidl_fuchsia_net::InterfaceAddress,
    address_parameters: fidl_fuchsia_net_interfaces_admin::AddressParameters,
) -> std::result::Result<
    fidl_fuchsia_net_interfaces_admin::AddressStateProviderProxy,
    fidl_fuchsia_net_interfaces_ext::admin::AddressStateProviderError,
> {
    let (address_state_provider, server) = fidl::endpoints::create_proxy::<
        fidl_fuchsia_net_interfaces_admin::AddressStateProviderMarker,
    >()
    .expect("create proxy");
    let () = control
        .add_address(&mut address, address_parameters, server)
        .expect("Control.AddAddress FIDL error");

    {
        let state_stream = fidl_fuchsia_net_interfaces_ext::admin::assignment_state_stream(
            address_state_provider.clone(),
        );
        futures::pin_mut!(state_stream);
        let () = fidl_fuchsia_net_interfaces_ext::admin::wait_assignment_state(
            &mut state_stream,
            fidl_fuchsia_net_interfaces_admin::AddressAssignmentState::Assigned,
        )
        .await?;
    }
    Ok(address_state_provider)
}

/// Wait until there is an IPv4 and an IPv6 link-local address assigned to the
/// interface identified by `id`.
///
/// If there are multiple IPv4 or multiple IPv6 link-local addresses assigned,
/// the choice of which particular address to return is arbitrary and should
/// not be relied upon.
///
/// Note that if a `netemul::TestInterface` is available, helpers on said type
/// should be preferred over using this function.
pub async fn wait_for_v4_and_v6_ll(
    interfaces_state: &fidl_fuchsia_net_interfaces::StateProxy,
    id: u64,
) -> Result<(net_types::ip::Ipv4Addr, net_types::ip::Ipv6Addr)> {
    wait_for_addresses(interfaces_state, id, |addresses| {
        let (v4, v6) = addresses.into_iter().fold(
            (None, None),
            |(v4, v6),
             &fidl_fuchsia_net_interfaces_ext::Address {
                 addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                 valid_until: _,
             }| {
                match addr {
                    fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address { addr }) => {
                        (Some(net_types::ip::Ipv4Addr::from(addr)), v6)
                    }
                    fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address { addr }) => {
                        let v6_addr = net_types::ip::Ipv6Addr::from_bytes(addr);
                        (v4, if v6_addr.is_unicast_linklocal() { Some(v6_addr) } else { v6 })
                    }
                }
            },
        );
        match (v4, v6) {
            (Some(v4), Some(v6)) => Some((v4, v6)),
            _ => None,
        }
    })
    .await
    .context("wait for addresses")
}

/// Wait until there is an IPv6 link-local address assigned to the interface
/// identified by `id`.
///
/// If there are multiple IPv6 link-local addresses assigned, the choice
/// of which particular address to return is arbitrary and should not be
/// relied upon.
///
/// Note that if a `netemul::TestInterface` is available, helpers on said type
/// should be preferred over using this function.
pub async fn wait_for_v6_ll(
    interfaces_state: &fidl_fuchsia_net_interfaces::StateProxy,
    id: u64,
) -> Result<net_types::ip::Ipv6Addr> {
    wait_for_addresses(interfaces_state, id, |addresses| {
        addresses.into_iter().find_map(
            |&fidl_fuchsia_net_interfaces_ext::Address {
                 addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                 valid_until: _,
             }| {
                match addr {
                    fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                        addr: _,
                    }) => None,
                    fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address { addr }) => {
                        let v6_addr = net_types::ip::Ipv6Addr::from_bytes(addr);
                        v6_addr.is_unicast_linklocal().then(|| v6_addr)
                    }
                }
            },
        )
    })
    .await
    .context("wait for IPv6 link-local address")
}

async fn wait_for_addresses<T, F>(
    interfaces_state: &fidl_fuchsia_net_interfaces::StateProxy,
    id: u64,
    mut predicate: F,
) -> Result<T>
where
    F: FnMut(&[fidl_fuchsia_net_interfaces_ext::Address]) -> Option<T>,
{
    let (watcher, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .context("failed to create fuchsia.net.interfaces/Watcher proxy")?;
    let () = interfaces_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server_end)
        .context("get_watcher")?;
    let mut state = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(u64::from(id));
    fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream(watcher),
        &mut state,
        |fidl_fuchsia_net_interfaces_ext::Properties {
             addresses,
             id: _,
             name: _,
             device_class: _,
             online: _,
             has_default_ipv4_route: _,
             has_default_ipv6_route: _,
         }| { predicate(addresses) },
    )
    .await
    .context("wait for address")
}
