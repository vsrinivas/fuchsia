// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Ping utilities.

use super::Result;

use std::convert::TryFrom as _;

use anyhow::Context as _;
use futures::{FutureExt as _, StreamExt as _};

/// A realm and associated data as a helper for issuing pings in tests.
pub struct Node<'a> {
    /// The test realm of this node.
    realm: &'a netemul::TestRealm<'a>,
    /// Local interface ID (used as scope ID when the destination IPv6 address
    /// is link-local).
    local_interface_id: u64,
    /// Local IPv4 addresses.
    v4_addrs: Vec<net_types::ip::Ipv4Addr>,
    /// Local IPv6 addresses.
    v6_addrs: Vec<net_types::ip::Ipv6Addr>,
}

impl<'a> Node<'a> {
    /// Constructs a new [`Node`].
    pub fn new(
        realm: &'a netemul::TestRealm<'_>,
        local_interface_id: u64,
        v4_addrs: Vec<net_types::ip::Ipv4Addr>,
        v6_addrs: Vec<net_types::ip::Ipv6Addr>,
    ) -> Self {
        Self { realm, local_interface_id, v4_addrs, v6_addrs }
    }

    /// Returns the local interface ID.
    pub fn id(&self) -> u64 {
        self.local_interface_id
    }

    /// Create a new [`Node`], waiting for addresses to satisfy the provided
    /// predicate.
    ///
    /// Addresses changes on `interface` will be observed via a watcher, and
    /// `addr_predicate` will be called with the addresses until the returned
    /// value is `Some`, and the vector of addresses will be used as the local
    /// addresses for this `Node`.
    pub async fn new_with_wait_addr<
        F: FnMut(
            &[fidl_fuchsia_net_interfaces_ext::Address],
        ) -> Option<(Vec<net_types::ip::Ipv4Addr>, Vec<net_types::ip::Ipv6Addr>)>,
    >(
        realm: &'a netemul::TestRealm<'_>,
        interface: &'a netemul::TestInterface<'_>,
        mut addr_predicate: F,
    ) -> Result<Node<'a>> {
        let mut state = fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(interface.id());
        let (v4_addrs, v6_addrs) = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
            interface.get_interface_event_stream()?,
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
        Ok(Self::new(realm, interface.id(), v4_addrs, v6_addrs))
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
                        }) => (Some(net_types::ip::Ipv4Addr::from(addr)), v6),
                        fidl_fuchsia_net::IpAddress::Ipv6(fidl_fuchsia_net::Ipv6Address {
                            addr,
                        }) => {
                            let v6_candidate = net_types::ip::Ipv6Addr::from_bytes(addr);
                            if v6_candidate.is_unicast_link_local() {
                                (v4, Some(v6_candidate))
                            } else {
                                (v4, v6)
                            }
                        }
                    }
                },
            );
            match (v4, v6) {
                (Some(v4), Some(v6)) => Some((vec![v4], vec![v6])),
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
                Node {
                    realm,
                    local_interface_id: src_id,
                    v4_addrs: src_v4_addrs,
                    v6_addrs: src_v6_addrs,
                },
                Node {
                    realm: _,
                    local_interface_id: _,
                    v4_addrs: dst_v4_addrs,
                    v6_addrs: dst_v6_addrs,
                },
            )| {
                const UNSPECIFIED_PORT: u16 = 0;
                let v4_futs = (!src_v4_addrs.is_empty()).then(|| {
                    dst_v4_addrs.iter().map(move |&addr| {
                        realm
                            .ping::<::ping::Ipv4>(std::net::SocketAddrV4::new(
                                std::net::Ipv4Addr::from(addr.ipv4_bytes()),
                                UNSPECIFIED_PORT,
                            ))
                            .left_future()
                    })
                });
                let v6_futs = (!src_v6_addrs.is_empty()).then(|| {
                    dst_v6_addrs.iter().map(move |&addr| {
                        let dst_sockaddr = std::net::SocketAddrV6::new(
                            std::net::Ipv6Addr::from(addr.ipv6_bytes()),
                            UNSPECIFIED_PORT,
                            0,
                            if addr.is_unicast_link_local() {
                                u32::try_from(*src_id).expect("interface ID does not fit into u32")
                            } else {
                                0
                            },
                        );
                        realm.ping::<::ping::Ipv6>(dst_sockaddr).right_future()
                    })
                });
                v4_futs.into_iter().flatten().chain(v6_futs.into_iter().flatten())
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
