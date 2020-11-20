// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Provides utilities for Netstack integration tests.

pub mod constants;
#[macro_use]
pub mod environments;

use std::collections::{HashMap, HashSet};
use std::convert::TryFrom;
use std::fmt::Debug;

use fidl_fuchsia_hardware_ethertap as ethertap;
use fidl_fuchsia_net_interfaces as net_interfaces;
use fuchsia_async::{self as fasync, DurationExt as _, TimeoutExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{FusedFuture, Future, FutureExt as _};
use futures::stream::{Stream, StreamExt, TryStreamExt};
use futures::TryFutureExt as _;
use net_types::ethernet::Mac;
use net_types::ip as net_types_ip;
use packet::serialize::{InnerPacketBuilder, Serializer};
use packet_formats::ethernet::{EtherType, EthernetFrameBuilder};
use packet_formats::icmp::ndp::{self, options::NdpOption};
use packet_formats::icmp::{IcmpMessage, IcmpPacketBuilder, IcmpUnusedCode};
use packet_formats::ip::IpProto;
use packet_formats::ipv6::Ipv6PacketBuilder;
use zerocopy::ByteSlice;

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

/// A trait that provides an Ethertap compatible name.
pub trait EthertapName {
    /// Returns an Ethertap compatible name.
    fn ethertap_compatible_name(&self) -> Self;
}

impl<'a> EthertapName for &'a str {
    fn ethertap_compatible_name(&self) -> &'a str {
        let max_len =
            usize::try_from(ethertap::MAX_NAME_LENGTH).expect("u32 could not fit into usize");
        &self[self.len().checked_sub(max_len).unwrap_or(0)..self.len()]
    }
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
    options: &[NdpOption<'_>],
    ep: &netemul::TestFakeEndpoint<'_>,
) -> Result {
    let ser = ndp::OptionsSerializer::<_>::new(options.iter())
        .into_serializer()
        .encapsulate(IcmpPacketBuilder::<_, B, _>::new(src_ip, dst_ip, IcmpUnusedCode, message))
        .encapsulate(Ipv6PacketBuilder::new(src_ip, dst_ip, NDP_MESSAGE_TTL, IpProto::Icmpv6))
        .encapsulate(EthernetFrameBuilder::new(src_mac, dst_mac, EtherType::Ipv6))
        .serialize_vec_outer()
        .map_err(|e| anyhow::anyhow!("failed to serialize NDP packet: {:?}", e))?
        .unwrap_b();
    let () = ep.write(ser.as_ref()).await.context("failed to write to fake endpoint")?;
    Ok(())
}

/// Waits for a non-loopback interface to come up with an ID not in `exclude_ids`.
///
/// Useful when waiting for an interface to be discovered and brought up by a
/// network manager.
///
/// Returns the interface's ID and name.
pub async fn wait_for_non_loopback_interface_up<
    F: Unpin + FusedFuture + Future<Output = Result<fuchsia_component::client::ExitStatus>>,
>(
    interface_state: &net_interfaces::StateProxy,
    mut wait_for_netmgr: &mut F,
    exclude_ids: Option<&HashSet<u32>>,
    timeout: zx::Duration,
) -> Result<(u32, String)> {
    let mut if_map = HashMap::new();
    let wait_for_interface = fidl_fuchsia_net_interfaces_ext::wait_interface(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(interface_state)?,
        &mut if_map,
        |if_map| {
            if_map.iter().find_map(|(id, properties)| {
                let id = *id as u32;
                if properties.device_class
                    != Some(net_interfaces::DeviceClass::Loopback(net_interfaces::Empty {}))
                    && properties.online?
                    && exclude_ids.map_or(true, |ids| !ids.contains(&id))
                {
                    Some((
                        id,
                        properties.name.clone().expect("failed to find loopback interface name"),
                    ))
                } else {
                    None
                }
            })
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
        wait_for_netmgr_res = wait_for_netmgr => {
            Err(anyhow::anyhow!("the network manager unexpectedly exited with exit status = {:?}", wait_for_netmgr_res?))
        }
    }
}
