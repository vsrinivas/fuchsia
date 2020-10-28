// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use std::collections::HashMap;
use std::convert::From as _;

use fuchsia_async as fasync;

use anyhow::Context as _;
use futures::{FutureExt as _, TryFutureExt as _, TryStreamExt as _};
use net_declare::{fidl_ip, fidl_mac};
use netemul::{
    Endpoint as _, EnvironmentUdpSocket as _, TestEnvironment, TestInterface, TestNetwork,
    TestSandbox,
};
use netstack_testing_common::environments::*;
use netstack_testing_common::Result;

const ALICE_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:00:01:02:03:04");
const ALICE_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.100);
const BOB_MAC: fidl_fuchsia_net::MacAddress = fidl_mac!("02:0A:0B:0C:0D:0E");
const BOB_IP: fidl_fuchsia_net::IpAddress = fidl_ip!(192.168.0.1);
const SUBNET_PREFIX: u8 = 24;

/// Helper function to create an environment with a static IP address and an
/// endpoint with a set MAC address.
///
/// Returns the created environment, ep, and the first observed assigned IPv6
/// address.
async fn create_environment<'a>(
    sandbox: &'a TestSandbox,
    network: &'a TestNetwork<'a>,
    test_name: &'static str,
    variant_name: &'static str,
    static_addr: fidl_fuchsia_net::Subnet,
    mac: fidl_fuchsia_net::MacAddress,
) -> Result<(TestEnvironment<'a>, TestInterface<'a>, fidl_fuchsia_net::IpAddress)> {
    let env = sandbox
        .create_netstack_environment::<Netstack2, _>(format!("{}_{}", test_name, variant_name))
        .context("failed to create environment")?;
    let ep = env
        .join_network_with(
            &network,
            format!("ep-{}", variant_name),
            netemul::NetworkDevice::make_config(netemul::DEFAULT_MTU, Some(mac)),
            &netemul::InterfaceConfig::StaticIp(static_addr),
        )
        .await
        .context("failed to join network")?;

    // Get IPv6 address.
    let interfaces = env
        .connect_to_service::<fidl_fuchsia_net_interfaces::StateMarker>()
        .context("failed to connect to interfaces.State")?;
    let addr = fidl_fuchsia_net_interfaces_ext::wait_interface_with_id(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces)
            .context("failed to get interfaces stream")?,
        &mut fidl_fuchsia_net_interfaces_ext::InterfaceState::Unknown(ep.id()),
        |props| {
            props.addresses.as_ref()?.iter().find_map(|addr| match addr.addr?.addr {
                a @ fidl_fuchsia_net::IpAddress::Ipv6(_) => Some(a.clone()),
                fidl_fuchsia_net::IpAddress::Ipv4(_) => None,
            })
        },
    )
    .await
    .context("failed to retrieve IPv6 address")?;

    Ok((env, ep, addr))
}

/// Gets a neighbor entry iterator stream with `options` in `env`.
fn get_entry_iterator(
    env: &TestEnvironment<'_>,
    options: fidl_fuchsia_net_neighbor::EntryIteratorOptions,
) -> Result<impl futures::Stream<Item = Result<fidl_fuchsia_net_neighbor::EntryIteratorItem>>> {
    let view = env
        .connect_to_service::<fidl_fuchsia_net_neighbor::ViewMarker>()
        .context("failed to connect to fuchsia.net.neighbor/View")?;
    let (proxy, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_neighbor::EntryIteratorMarker>()
            .context("failed to create EntryIterator proxy")?;
    let () =
        view.open_entry_iterator(server_end, options).context("failed to open EntryIterator")?;
    Ok(futures::stream::try_unfold(proxy, |proxy| {
        proxy
            .get_next()
            .map(|r| r.context("fuchsia.net.neighbor/EntryIterator.GetNext FIDL error"))
            .map_ok(|it| Some((futures::stream::iter(it.into_iter().map(Result::Ok)), proxy)))
    })
    .try_flatten())
}

/// Retrieves all existing neighbor entries in `env`.
///
/// Entries are identified by unique `(interface_id, ip_address)` tuples in the
/// returned map.
async fn list_existing_entries(
    env: &TestEnvironment<'_>,
) -> Result<HashMap<(u64, fidl_fuchsia_net::IpAddress), fidl_fuchsia_net_neighbor::Entry>> {
    use async_utils::fold::*;
    try_fold_while(
        get_entry_iterator(env, fidl_fuchsia_net_neighbor::EntryIteratorOptions::empty())?,
        HashMap::new(),
        |mut map, item| {
            futures::future::ready(match item {
                fidl_fuchsia_net_neighbor::EntryIteratorItem::Existing(e) => {
                    if let fidl_fuchsia_net_neighbor::Entry {
                        interface: Some(interface),
                        neighbor: Some(neighbor),
                        ..
                    } = &e
                    {
                        if let Some(e) = map.insert((*interface, neighbor.clone()), e) {
                            Err(anyhow::anyhow!("duplicate entry detected in map: {:?}", e))
                        } else {
                            Ok(FoldWhile::Continue(map))
                        }
                    } else {
                        Err(anyhow::anyhow!(
                            "missing interface or neighbor in existing entry: {:?}",
                            e
                        ))
                    }
                }
                fidl_fuchsia_net_neighbor::EntryIteratorItem::Idle(
                    fidl_fuchsia_net_neighbor::IdleEvent {},
                ) => Ok(FoldWhile::Done(map)),
                x @ fidl_fuchsia_net_neighbor::EntryIteratorItem::Added(_)
                | x @ fidl_fuchsia_net_neighbor::EntryIteratorItem::Changed(_)
                | x @ fidl_fuchsia_net_neighbor::EntryIteratorItem::Removed(_) => {
                    Err(anyhow::anyhow!("unexpected EntryIteratorItem before Idle: {:?}", x))
                }
            })
        },
    )
    .await
    .and_then(|r| {
        r.short_circuited().map_err(|e| {
            anyhow::anyhow!("entry iterator stream ended unexpectedly with state {:?}", e)
        })
    })
}

/// Helper function to exchange a single UDP datagram.
///
/// `alice` will send a single UDP datagram to `bob`. This function will block
/// until `bob` receives the datagram.
async fn exchange_dgram(
    alice: &TestEnvironment<'_>,
    alice_addr: fidl_fuchsia_net::IpAddress,
    bob: &TestEnvironment<'_>,
    bob_addr: fidl_fuchsia_net::IpAddress,
) -> Result {
    let fidl_fuchsia_net_ext::IpAddress(alice_addr) =
        fidl_fuchsia_net_ext::IpAddress::from(alice_addr);
    let alice_addr = std::net::SocketAddr::new(alice_addr, 1234);

    let fidl_fuchsia_net_ext::IpAddress(bob_addr) = fidl_fuchsia_net_ext::IpAddress::from(bob_addr);
    let bob_addr = std::net::SocketAddr::new(bob_addr, 8080);

    let alice_sock = fuchsia_async::net::UdpSocket::bind_in_env(alice, alice_addr)
        .await
        .context("failed to create client socket")?;

    let bob_sock = fuchsia_async::net::UdpSocket::bind_in_env(bob, bob_addr)
        .await
        .context("failed to create server socket")?;

    const PAYLOAD: &'static str = "Hello Neighbor";
    let mut buf = [0u8; 512];
    let (sent, (rcvd, from)) = futures::future::try_join(
        alice_sock.send_to(PAYLOAD.as_bytes(), bob_addr).map(|r| r.context("UDP send_to failed")),
        bob_sock.recv_from(&mut buf[..]).map(|r| r.context("UDP recv_from failed")),
    )
    .await?;
    assert_eq!(sent, PAYLOAD.as_bytes().len());
    assert_eq!(rcvd, PAYLOAD.as_bytes().len());
    assert_eq!(&buf[..rcvd], PAYLOAD.as_bytes());
    // Check equality on IP and port separately since for IPv6 the scope ID may
    // differ, making a direct equality fail.
    assert_eq!(from.ip(), alice_addr.ip());
    assert_eq!(from.port(), alice_addr.port());
    Ok(())
}

/// Helper function to assert validity of a reachable entry.
fn assert_reachable_entry(
    entry: fidl_fuchsia_net_neighbor::Entry,
    match_iface: u64,
    match_neighbor: fidl_fuchsia_net::IpAddress,
    match_mac: fidl_fuchsia_net::MacAddress,
) {
    match entry {
        fidl_fuchsia_net_neighbor::Entry {
            interface: Some(iface),
            neighbor: Some(neighbor),
            state:
                Some(fidl_fuchsia_net_neighbor::EntryState::Reachable(
                    // TODO(fxbug.dev/59372): Capture and assert expiration
                    // value
                    fidl_fuchsia_net_neighbor::ReachableState { expires_at: None },
                )),
            mac: Some(mac),
            updated_at: Some(updated),
        } => {
            assert_eq!(iface, match_iface);
            assert_eq!(neighbor, match_neighbor);
            assert_eq!(mac, match_mac);
            assert!(updated > 0, "expected greater than 0, got: {}", updated);
        }
        x => panic!("incomplete or bad state reachable neighbor entry: {:?}", x),
    }
}

/// Helper function to assert validity of a stale entry.
fn assert_stale_entry(
    entry: fidl_fuchsia_net_neighbor::Entry,
    match_iface: u64,
    match_neighbor: fidl_fuchsia_net::IpAddress,
    match_mac: fidl_fuchsia_net::MacAddress,
) {
    match entry {
        fidl_fuchsia_net_neighbor::Entry {
            interface: Some(iface),
            neighbor: Some(neighbor),
            state:
                Some(fidl_fuchsia_net_neighbor::EntryState::Stale(
                    fidl_fuchsia_net_neighbor::StaleState {},
                )),
            mac: Some(mac),
            updated_at: Some(updated),
        } => {
            assert_eq!(iface, match_iface);
            assert_eq!(neighbor, match_neighbor);
            assert_eq!(mac, match_mac);
            assert!(updated > 0, "expected greater than 0, got: {}", updated);
        }
        x => panic!("incomplete or bad state stale neighbor entry: {:?}", x),
    }
}

#[fasync::run_singlethreaded(test)]
async fn neigh_list_entries() -> Result {
    // TODO(fxbug.dev/59425): Extend this test with hanging get.

    const TEST_NAME: &'static str = "neigh_list_entries";
    let sandbox = TestSandbox::new().context("failed to create sandbox")?;
    let network = sandbox.create_network("net").await.context("failed to create network")?;

    let (alice_env, alice_iface, alice_ipv6) = create_environment(
        &sandbox,
        &network,
        TEST_NAME,
        "alice",
        fidl_fuchsia_net::Subnet { addr: ALICE_IP, prefix_len: SUBNET_PREFIX },
        ALICE_MAC,
    )
    .await
    .context("failed to setup alice environment")?;

    let (bob_env, bob_iface, bob_ipv6) = create_environment(
        &sandbox,
        &network,
        TEST_NAME,
        "bob",
        fidl_fuchsia_net::Subnet { addr: BOB_IP, prefix_len: SUBNET_PREFIX },
        BOB_MAC,
    )
    .await
    .context("failed to setup bob environment")?;

    // No Neighbors should exist initially.
    let alice_entries =
        list_existing_entries(&alice_env).await.context("failed to get entries for alice")?;
    assert!(alice_entries.is_empty(), "expected empty set of entries: {:?}", alice_entries);
    let bob_entries =
        list_existing_entries(&bob_env).await.context("failed to get entries for bob")?;
    assert!(bob_entries.is_empty(), "expected empty set of entries: {:?}", bob_entries);

    // Send a single UDP datagram between alice and bob.
    let () = exchange_dgram(&alice_env, ALICE_IP, &bob_env, BOB_IP)
        .await
        .context("IPv4 exchange failed")?;
    let () = exchange_dgram(&alice_env, alice_ipv6, &bob_env, bob_ipv6)
        .await
        .context("IPv6 exchange failed")?;

    // Check that bob is listed as a neighbor for alice.
    let mut alice_entries =
        list_existing_entries(&alice_env).await.context("failed to get entries for alice")?;
    // IPv4 entry.
    let () = assert_reachable_entry(
        alice_entries.remove(&(alice_iface.id(), BOB_IP)).expect("missing neighbor entry"),
        alice_iface.id(),
        BOB_IP,
        BOB_MAC,
    );
    // IPv6 entry.
    let () = assert_reachable_entry(
        alice_entries.remove(&(alice_iface.id(), bob_ipv6)).expect("missing neighbor entry"),
        alice_iface.id(),
        bob_ipv6,
        BOB_MAC,
    );
    assert!(
        alice_entries.is_empty(),
        "unexpected neighbors remaining in list: {:?}",
        alice_entries
    );

    // Check that alice is listed as a neighbor for bob. Bob should have alice
    // listed as STALE entries due to having received solicitations as part of
    // the UDP exchange.
    let mut bob_entries =
        list_existing_entries(&bob_env).await.context("failed to get entries for bob")?;

    // IPv4 entry.
    let () = assert_stale_entry(
        bob_entries.remove(&(bob_iface.id(), ALICE_IP)).expect("missing neighbor entry"),
        bob_iface.id(),
        ALICE_IP,
        ALICE_MAC,
    );
    // IPv6 entry.
    let () = assert_stale_entry(
        bob_entries.remove(&(bob_iface.id(), alice_ipv6)).expect("missing neighbor entry"),
        bob_iface.id(),
        alice_ipv6,
        ALICE_MAC,
    );
    assert!(bob_entries.is_empty(), "unexpected neighbors remaining in list: {:?}", bob_entries);

    Ok(())
}
