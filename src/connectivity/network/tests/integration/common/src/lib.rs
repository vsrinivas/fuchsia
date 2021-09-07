// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Provides utilities for Netstack integration tests.

pub mod constants;
#[macro_use]
pub mod realms;

use std::collections::{HashMap, HashSet};
use std::fmt::Debug;

use fidl_fuchsia_net_interfaces as fnet_interfaces;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_netstack as fnetstack;
use fidl_fuchsia_sys2 as fsys2;
use fuchsia_async::{self as fasync, DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{FusedFuture, Future, FutureExt as _};
use futures::stream::{Stream, StreamExt as _, TryStreamExt as _};
use futures::TryFutureExt as _;
use net_types::ethernet::Mac;
use net_types::ip as net_types_ip;
use net_types::Witness as _;
use packet::serialize::{InnerPacketBuilder, Serializer};
use packet_formats::ethernet::{EtherType, EthernetFrameBuilder};
use packet_formats::icmp::ndp::{self, options::NdpOptionBuilder, RouterAdvertisement};
use packet_formats::icmp::{IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode};
use packet_formats::ip::Ipv6Proto;
use packet_formats::ipv6::Ipv6PacketBuilder;
use zerocopy::ByteSlice;

use crate::realms::TestSandboxExt as _;

/// An alias for `Result<T, anyhow::Error>`.
pub type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// Extra time to use when waiting for an async event to occur.
///
/// A large timeout to help prevent flakes.
pub const ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT: zx::Duration = zx::Duration::from_seconds(120);

/// Extra time to use when waiting for an async event to not occur.
///
/// Since a negative check is used to make sure an event did not happen, its okay to use a
/// smaller timeout compared to the positive case since execution stall in regards to the
/// monotonic clock will not affect the expected outcome.
pub const ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT: zx::Duration = zx::Duration::from_seconds(5);

/// The time to wait between two consecutive checks of an event.
pub const ASYNC_EVENT_CHECK_INTERVAL: zx::Duration = zx::Duration::from_seconds(1);

/// As per [RFC 4861] sections 4.1-4.5, NDP packets MUST have a hop limit of 255.
///
/// [RFC 4861]: https://tools.ietf.org/html/rfc4861
pub const NDP_MESSAGE_TTL: u8 = 255;

/// Returns `true` once the stream yields a `true`.
///
/// If the stream never yields `true` or never terminates, `try_any` may never resolve.
pub async fn try_any<S: Stream<Item = Result<bool>>>(stream: S) -> Result<bool> {
    futures::pin_mut!(stream);
    stream.try_filter(|v| futures::future::ready(*v)).next().await.unwrap_or(Ok(false))
}

/// Returns `true` if the stream only yields `true`.
///
/// If the stream never yields `false` or never terminates, `try_all` may never resolve.
pub async fn try_all<S: Stream<Item = Result<bool>>>(stream: S) -> Result<bool> {
    futures::pin_mut!(stream);
    stream.try_filter(|v| futures::future::ready(!*v)).next().await.unwrap_or(Ok(true))
}

/// Asynchronously sleeps for specified `secs` seconds.
pub async fn sleep(secs: i64) {
    fasync::Timer::new(zx::Duration::from_seconds(secs).after_now()).await;
}

/// Writes an NDP message to the provided fake endpoint.
///
/// Given the source and destination MAC and IP addresses, NDP message and
/// options, the full NDP packet (including IPv6 and Ethernet headers) will be
/// transmitted to the fake endpoint's network.
pub async fn write_ndp_message<
    B: ByteSlice + Debug,
    M: IcmpMessage<net_types_ip::Ipv6, B, Code = IcmpUnusedCode> + Debug,
>(
    src_mac: Mac,
    dst_mac: Mac,
    src_ip: net_types_ip::Ipv6Addr,
    dst_ip: net_types_ip::Ipv6Addr,
    message: M,
    options: &[NdpOptionBuilder<'_>],
    ep: &netemul::TestFakeEndpoint<'_>,
) -> Result {
    let ser = ndp::OptionsSerializer::<_>::new(options.iter())
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, B, _>::new(src_ip, dst_ip, IcmpUnusedCode, message))
        .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, NDP_MESSAGE_TTL, Ipv6Proto::Icmpv6))
        .encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, EtherType::Ipv6))
        .serialize_vec_outer()
        .map_err(|e| anyhow::anyhow!("failed to serialize NDP packet: {:?}", e))?
        .unwrap_b();
    ep.write(ser.as_ref()).await.context("failed to write to fake endpoint")
}

/// Waits for a `stopped` event to be emitted for a component in a test realm.
///
/// Optionally specifies a matcher for the expected exit status of the `stopped` event.
pub async fn wait_for_component_stopped(
    realm: &netemul::TestRealm<'_>,
    component_moniker: &str,
    status_matcher: Option<component_events::matcher::ExitStatusMatcher>,
) -> Result<component_events::events::Stopped> {
    use component_events::{
        events::{self, Event as _, EventMode, EventSource, EventSubscription},
        matcher::EventMatcher,
    };

    let event_source = EventSource::from_proxy(
        fuchsia_component::client::connect_to_protocol::<fsys2::EventSourceMarker>()
            .context("failed to connect to event source protocol")?,
    );
    let mut event_stream = event_source
        .subscribe(vec![EventSubscription::new(vec![events::Stopped::NAME], EventMode::Async)])
        .await
        .context("failed to subscribe to `Stopped` events")?;
    let realm_moniker = &realm.get_moniker().await.context("calling get moniker")?;
    let moniker_for_match = format!(
        "{}:\\d+/{}:\\d+/{}:\\d+",
        NETEMUL_SANDBOX_MONIKER, realm_moniker, component_moniker
    );
    EventMatcher::ok()
        .stop(status_matcher)
        .moniker(moniker_for_match)
        .wait::<events::Stopped>(&mut event_stream)
        .await
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
    interface_state: &fnet_interfaces::StateProxy,
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
                        != fnet_interfaces::DeviceClass::Loopback(fnet_interfaces::Empty {})
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

/// Waits for an interface to come up with the specified address.
pub async fn wait_for_interface_up_and_address(
    state: &fidl_fuchsia_net_interfaces::StateProxy,
    id: u64,
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

/// The name of the netemul sandbox component, which is the parent component of
/// managed test realms.
const NETEMUL_SANDBOX_MONIKER: &str = "sandbox";

/// Gets the moniker of a component in a test realm, relative to the root of the
/// dynamic collection in which it is running.
pub async fn get_component_moniker<'a>(
    realm: &netemul::TestRealm<'a>,
    component: &str,
) -> Result<String> {
    let realm_moniker = realm.get_moniker().await.context("calling get moniker")?;
    Ok([NETEMUL_SANDBOX_MONIKER, &realm_moniker, component].join("/"))
}

/// Gets inspect data in realm.
///
/// Returns the resulting inspect data for `component`, filtered by
/// `tree_selector` and with inspect file starting with `file_prefix`.
pub async fn get_inspect_data(
    realm: &netemul::TestRealm<'_>,
    component_moniker: impl Into<String>,
    tree_selector: impl Into<String>,
    file_prefix: &str,
) -> Result<diagnostics_hierarchy::DiagnosticsHierarchy> {
    let realm_moniker = selectors::sanitize_string_for_selectors(
        &realm.get_moniker().await.context("calling get moniker")?,
    );
    let mut data = diagnostics_reader::ArchiveReader::new()
        .add_selector(
            diagnostics_reader::ComponentSelector::new(vec![
                NETEMUL_SANDBOX_MONIKER.into(),
                realm_moniker,
                component_moniker.into(),
            ])
            .with_tree_selector(tree_selector.into()),
        )
        // Enable `retry_if_empty` to prevent races in test realm bringup where
        // we may end up reaching `ArchiveReader` before it has observed
        // Netstack starting.
        //
        // Eventually there will be support for lifecycle streams, with which
        // it will be possible to wait on the event of Archivist obtaining a
        // handle to Netstack diagnostics, and then request the snapshot of
        // inspect data once that event is received.
        .retry_if_empty(true)
        .snapshot::<diagnostics_reader::Inspect>()
        .await
        .context("failed to get inspect data")?
        .into_iter()
        .filter_map(
            |diagnostics_data::InspectData {
                 data_source: _,
                 metadata,
                 moniker: _,
                 payload,
                 version: _,
             }| {
                if metadata.filename.starts_with(file_prefix) {
                    Some(payload.ok_or_else(|| {
                        anyhow::anyhow!(
                            "empty inspect payload, metadata errors: {:?}",
                            metadata.errors
                        )
                    }))
                } else {
                    None
                }
            },
        );
    let datum = data.next().unwrap_or_else(|| Err(anyhow::anyhow!("failed to find inspect data")));
    let data: Vec<_> = data.collect();
    assert!(
        data.is_empty(),
        "expected a single inspect entry; got {:?} and also {:?}",
        datum,
        data
    );
    datum
}

/// Send Router Advertisement NDP message with router lifetime.
pub async fn send_ra_with_router_lifetime<'a>(
    fake_ep: &netemul::TestFakeEndpoint<'a>,
    lifetime: u16,
    options: &[NdpOptionBuilder<'_>],
) -> Result {
    let ra = RouterAdvertisement::new(
        0,        /* current_hop_limit */
        false,    /* managed_flag */
        false,    /* other_config_flag */
        lifetime, /* router_lifetime */
        0,        /* reachable_time */
        0,        /* retransmit_timer */
    );
    write_ndp_message::<&[u8], _>(
        constants::eth::MAC_ADDR,
        Mac::from(&net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS),
        constants::ipv6::LINK_LOCAL_ADDR,
        net_types_ip::Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
        ra,
        options,
        fake_ep,
    )
    .await
}

/// Sets up a realm with a network with no required services.
pub async fn setup_network<E, S>(
    sandbox: &netemul::TestSandbox,
    // TODO(https://fxbug.dev/84137): Change type to `Cow<&'static, str>`.
    name: S,
) -> Result<(
    netemul::TestNetwork<'_>,
    netemul::TestRealm<'_>,
    fnetstack::NetstackProxy,
    netemul::TestInterface<'_>,
    netemul::TestFakeEndpoint<'_>,
)>
where
    E: netemul::Endpoint,
    S: Copy + Into<String>,
{
    setup_network_with::<E, S, _>(sandbox, name, std::iter::empty::<fnetemul::ChildDef>()).await
}

/// Sets up a realm with required services and a network used for tests
/// requiring manual packet inspection and transmission.
///
/// Returns the network, realm, netstack client, interface (added to the
/// netstack and up) and a fake endpoint used to read and write raw ethernet
/// packets.
pub async fn setup_network_with<E, S, I>(
    sandbox: &netemul::TestSandbox,
    // TODO(https://fxbug.dev/84137): Change type to `Cow<&'static, str>`.
    name: S,
    children: I,
) -> Result<(
    netemul::TestNetwork<'_>,
    netemul::TestRealm<'_>,
    fnetstack::NetstackProxy,
    netemul::TestInterface<'_>,
    netemul::TestFakeEndpoint<'_>,
)>
where
    E: netemul::Endpoint,
    S: Copy + Into<String>,
    I: IntoIterator,
    I::Item: Into<fnetemul::ChildDef>,
{
    let network = sandbox.create_network(name).await.context("failed to create network")?;
    let realm = sandbox
        .create_netstack_realm_with::<realms::Netstack2, _, _>(name, children)
        .context("failed to create netstack realm")?;
    // It is important that we create the fake endpoint before we join the
    // network so no frames transmitted by Netstack are lost.
    let fake_ep = network.create_fake_endpoint()?;

    let iface = realm
        .join_network::<E, _>(&network, name, &netemul::InterfaceConfig::None)
        .await
        .context("failed to configure networking")?;

    let netstack = realm
        .connect_to_protocol::<fnetstack::NetstackMarker>()
        .context("failed to connect to netstack protocol")?;

    Ok((network, realm, netstack, iface, fake_ep))
}
