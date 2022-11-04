// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Provides utilities for Netstack integration tests.

pub mod constants;
pub mod devices;
pub mod interfaces;
pub mod packets;
pub mod ping;
#[macro_use]
pub mod realms;

use std::fmt::Debug;

use component_events::events::EventStream;
use fidl_fuchsia_netemul as fnetemul;
use fidl_fuchsia_netstack as fnetstack;
use fuchsia_async::{self as fasync, DurationExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::stream::{Stream, StreamExt as _, TryStreamExt as _};
use net_types::{ethernet::Mac, ip as net_types_ip, Witness as _};
use packet::serialize::{InnerPacketBuilder, Serializer};
use packet_formats::{
    ethernet::{EtherType, EthernetFrameBuilder},
    icmp::{
        ndp::{self, options::NdpOptionBuilder, RouterAdvertisement},
        IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode,
    },
    ip::Ipv6Proto,
    ipv6::Ipv6PacketBuilder,
};
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
    let ser = ndp::OptionSequenceBuilder::new(options.iter())
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, B, _>::new(src_ip, dst_ip, IcmpUnusedCode, message))
        .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, NDP_MESSAGE_TTL, Ipv6Proto::Icmpv6))
        .encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, EtherType::Ipv6))
        .serialize_vec_outer()
        .map_err(|e| anyhow::anyhow!("failed to serialize NDP packet: {:?}", e))?
        .unwrap_b();
    ep.write(ser.as_ref()).await.context("failed to write to fake endpoint")
}

/// Gets a component event stream yielding component stopped events.
pub async fn get_component_stopped_event_stream() -> Result<component_events::events::EventStream> {
    EventStream::open_at_path("/events/stopped")
        .await
        .context("failed to subscribe to `Stopped` events")
}

/// Waits for a `stopped` event to be emitted for a component in a test realm.
///
/// Optionally specifies a matcher for the expected exit status of the `stopped`
/// event.
pub async fn wait_for_component_stopped_with_stream(
    event_stream: &mut component_events::events::EventStream,
    realm: &netemul::TestRealm<'_>,
    component_moniker: &str,
    status_matcher: Option<component_events::matcher::ExitStatusMatcher>,
) -> Result<component_events::events::Stopped> {
    let matcher = get_child_component_event_matcher(realm, component_moniker)
        .await
        .context("get child component matcher")?;
    matcher.stop(status_matcher).wait::<component_events::events::Stopped>(event_stream).await
}

/// Like [`wait_for_component_stopped_with_stream`] but retrieves an event
/// stream for the caller.
///
/// Note that this function fails to observe stop events that happen in early
/// realm creation, which is especially true for eager components.
pub async fn wait_for_component_stopped(
    realm: &netemul::TestRealm<'_>,
    component_moniker: &str,
    status_matcher: Option<component_events::matcher::ExitStatusMatcher>,
) -> Result<component_events::events::Stopped> {
    let mut stream = get_component_stopped_event_stream().await?;
    wait_for_component_stopped_with_stream(&mut stream, realm, component_moniker, status_matcher)
        .await
}

/// Gets an event matcher for `component_moniker` in `realm`.
pub async fn get_child_component_event_matcher(
    realm: &netemul::TestRealm<'_>,
    component_moniker: &str,
) -> Result<component_events::matcher::EventMatcher> {
    let realm_moniker = &realm.get_moniker().await.context("calling get moniker")?;
    let moniker_for_match =
        format!("./{}/{}/{}", NETEMUL_SANDBOX_MONIKER, realm_moniker, component_moniker);
    Ok(component_events::matcher::EventMatcher::ok().moniker(moniker_for_match))
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
    let mut archive_reader = diagnostics_reader::ArchiveReader::new();
    let _archive_reader_ref = archive_reader
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
        // the component starting.
        //
        // Eventually there will be support for lifecycle streams, with which it
        // will be possible to wait on the event of Archivist obtaining a handle
        // to the component's diagnostics, and then request the snapshot of
        // inspect data once that event is received.
        .retry_if_empty(true);

    // Loop to wait for the component to begin publishing inspect data after it
    // starts.
    loop {
        let mut data = archive_reader
            .snapshot::<diagnostics_reader::Inspect>()
            .await
            .context("snapshot did not return any inspect data")?
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
        match data.next() {
            Some(datum) => {
                let data: Vec<_> = data.collect();
                assert!(
                    data.is_empty(),
                    "expected a single inspect entry; got {:?} and also {:?}",
                    datum,
                    data
                );
                return datum;
            }
            None => {
                fasync::Timer::new(zx::Duration::from_millis(100).after_now()).await;
            }
        }
    }
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
pub async fn setup_network<'a, E>(
    sandbox: &'a netemul::TestSandbox,
    name: &'a str,
    metric: Option<u32>,
) -> Result<(
    netemul::TestNetwork<'a>,
    netemul::TestRealm<'a>,
    fnetstack::NetstackProxy,
    netemul::TestInterface<'a>,
    netemul::TestFakeEndpoint<'a>,
)>
where
    E: netemul::Endpoint,
{
    setup_network_with::<E, _>(sandbox, name, metric, std::iter::empty::<fnetemul::ChildDef>())
        .await
}

/// Sets up a realm with required services and a network used for tests
/// requiring manual packet inspection and transmission.
///
/// Returns the network, realm, netstack client, interface (added to the
/// netstack and up) and a fake endpoint used to read and write raw ethernet
/// packets.
pub async fn setup_network_with<'a, E, I>(
    sandbox: &'a netemul::TestSandbox,
    name: &'a str,
    metric: Option<u32>,
    children: I,
) -> Result<(
    netemul::TestNetwork<'a>,
    netemul::TestRealm<'a>,
    fnetstack::NetstackProxy,
    netemul::TestInterface<'a>,
    netemul::TestFakeEndpoint<'a>,
)>
where
    E: netemul::Endpoint,
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
        .join_network_with_if_config::<E, _>(
            &network,
            name,
            netemul::InterfaceConfig { name: Some(name.into()), metric },
        )
        .await
        .context("failed to configure networking")?;

    let netstack = realm
        .connect_to_protocol::<fnetstack::NetstackMarker>()
        .context("failed to connect to netstack protocol")?;

    Ok((network, realm, netstack, iface, fake_ep))
}

/// Pauses the fake clock in the given realm.
pub async fn pause_fake_clock(realm: &netemul::TestRealm<'_>) -> Result<()> {
    let fake_clock_control = realm
        .connect_to_protocol::<fidl_fuchsia_testing::FakeClockControlMarker>()
        .context("failed to connect to FakeClockControl")?;
    let () = fake_clock_control.pause().await.context("failed to pause time")?;
    Ok(())
}
