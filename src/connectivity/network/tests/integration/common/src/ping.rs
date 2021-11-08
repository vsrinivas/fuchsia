// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ping utilities.

use super::Result;

use std::convert::TryFrom as _;

use anyhow::Context as _;
use futures::{FutureExt as _, StreamExt as _};
// TODO(https://fxbug.dev/87626): Remove the macro import when a newer version
// of itertools which fixes the import hygiene issue is available.
use itertools::iproduct;

/// A realm and associated data as a helper for issuing pings in tests.
pub struct Node<'a> {
    /// The test realm of this node.
    realm: &'a netemul::TestRealm<'a>,
    /// The local interface ID to be used to as the IPv6 scope ID if pinging
    /// a link-local IPv6 address.
    local_interface_id: u64,
    /// Local addresses that should be reachable via ping messages.
    local_addresses: Vec<std::net::IpAddr>,
}

impl<'a> Node<'a> {
    /// Constructs a new [`Node`].
    pub fn new(
        realm: &'a netemul::TestRealm<'_>,
        local_interface_id: u64,
        local_addresses: Vec<std::net::IpAddr>,
    ) -> Self {
        Self { realm, local_interface_id, local_addresses }
    }

    /// Create a new [`Node`], waiting for addresses to satisfy the provided
    /// predicate.
    ///
    /// Addresses changes on `interface` will be observed via a watcher, and
    /// `addr_predicate` will be called with the addresses until the returned
    /// value is `Some`, and the vector of addresses will be used as the local
    /// addresses for this `Node`.
    pub async fn new_with_wait_addr<
        F: FnMut(&[fidl_fuchsia_net_interfaces_ext::Address]) -> Option<Vec<std::net::IpAddr>>,
    >(
        realm: &'a netemul::TestRealm<'_>,
        interface: &'a netemul::TestInterface<'_>,
        mut addr_predicate: F,
    ) -> Result<Node<'a>> {
        let mut state = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(interface.id());
        let local_addresses = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            fidl_fuchsia_net_interfaces_ext::event_stream(
                interface
                    .get_interfaces_watcher()
                    .context("failed to get interface property watcher to wait for addresses")?,
            ),
            &mut state,
            |fidl_fuchsia_net_interfaces_ext::Properties {
                 addresses,
                 id: _,
                 name: _,
                 device_class: _,
                 online: _,
                 has_default_ipv4_route: _,
                 has_default_ipv6_route: _,
             }| { addr_predicate(addresses) },
        )
        .await
        .context("failed to wait for addresses")?;
        Ok(Self::new(realm, interface.id(), local_addresses))
    }

    /// Create a new [`Node`] with one IPv4 address and one IPv6 link-local
    /// address.
    ///
    /// Note that if there are multiple addresses of either kind assigned to
    /// the interface, the specific address chosen is potentially
    /// non-deterministic. Callers that care about specific addresses being
    /// included rather than any IPv4 address and any IPv6 link-local address
    /// should prefer [`Self::new_with_wait_addr`] instead.
    pub async fn new_with_v4_and_v6_link_local(
        realm: &'a netemul::TestRealm<'_>,
        interface: &'a netemul::TestInterface<'_>,
    ) -> Result<Node<'a>> {
        Self::new_with_wait_addr(realm, interface, |addresses| {
            let (v4, v6) = addresses.into_iter().fold(
                (None, None),
                |(v4, v6),
                 &fidl_fuchsia_net_interfaces_ext::Address {
                     addr: fidl_fuchsia_net::Subnet { addr, prefix_len: _ },
                     valid_until: _,
                 }| {
                    match addr {
                        fidl_fuchsia_net::IpAddress::Ipv4(fidl_fuchsia_net::Ipv4Address {
                            addr,
                        }) => (Some(std::net::IpAddr::from(addr)), v6),
                        fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                            addr,
                        }) => {
                            if net_types::ip::Ipv6Addr::from_bytes(addr).is_unicast_linklocal() {
                                (v4, Some(std::net::IpAddr::from(addr)))
                            } else {
                                (v4, v6)
                            }
                        }
                    }
                },
            );
            match (v4, v6) {
                (Some(v4), Some(v6)) => Some(vec![v4, v6]),
                _ => None,
            }
        })
        .await
    }

    /// Returns `Ok(())` iff every possible pair in the union of `self` and
    /// `pingable` is able to ping each other.
    pub async fn ping_pairwise(
        &self,
        pingable: &[Node<'_>],
    ) -> anyhow::Result<(), Vec<anyhow::Error>> {
        // NB: The interface ID of the sender is used as the scope_id for IPv6.
        let futs = itertools::iproduct!(
            pingable.iter().chain(std::iter::once(self)),
            pingable.iter().chain(std::iter::once(self))
        )
        .map(
            |(
                &Node { realm, local_interface_id, local_addresses: _ },
                Node { realm: _, local_interface_id: _, local_addresses },
            )| {
                const UNSPECIFIED_PORT: u16 = 0;
                local_addresses.iter().map(move |&addr| match addr {
                    std::net::IpAddr::V4(addr_v4) => realm
                        .ping::<::ping::Ipv4>(std::net::SocketAddrV4::new(
                            addr_v4,
                            UNSPECIFIED_PORT,
                        ))
                        .left_future(),
                    std::net::IpAddr::V6(addr_v6) => {
                        let scope_id = if net_types::ip::Ipv6Addr::from_bytes(addr_v6.octets())
                            .is_unicast_linklocal()
                        {
                            u32::try_from(local_interface_id).expect("ID doesn't fit into u32")
                        } else {
                            0
                        };
                        realm
                            .ping::<::ping::Ipv6>(std::net::SocketAddrV6::new(
                                addr_v6,
                                UNSPECIFIED_PORT,
                                0,
                                scope_id,
                            ))
                            .right_future()
                    }
                })
            },
        )
        .flatten()
        .collect::<futures::stream::FuturesUnordered<_>>();
        let errors = futs
            .filter_map(|r| {
                futures::future::ready(match r {
                    Ok(()) => None,
                    Err(e) => Some(e),
                })
            })
            .collect::<Vec<_>>()
            .await;
        if errors.is_empty() {
            Ok(())
        } else {
            Err(errors)
        }
    }
}
