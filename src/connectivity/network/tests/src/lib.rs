// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod constants;
mod dhcp;
mod dns;
mod management;
#[macro_use]
mod environments;
mod fidl;
mod inspect;
mod ipv6;
mod routes;
mod socket;

use std::convert::TryFrom;

use fidl_fuchsia_hardware_ethertap as ethertap;
use fuchsia_async::{self as fasync, DurationExt as _};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::stream::{Stream, TryStreamExt};

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// Extra time to use when waiting for an async event to occur.
///
/// A large timeout to help prevent flakes.
const ASYNC_EVENT_POSITIVE_CHECK_TIMEOUT: zx::Duration = zx::Duration::from_seconds(120);

/// Extra time to use when waiting for an async event to not occur.
///
/// Since a negative check is used to make sure an event did not happen, its okay to use a
/// smaller timeout compared to the positive case since execution stall in regards to the
/// monotonic clock will not affect the expected outcome.
const ASYNC_EVENT_NEGATIVE_CHECK_TIMEOUT: zx::Duration = zx::Duration::from_seconds(5);

/// The time to wait between two consecutive checks of an event.
const ASYNC_EVENT_CHECK_INTERVAL: zx::Duration = zx::Duration::from_seconds(1);

/// The path to the default configuration file for DHCP server.
const DHCP_SERVER_DEFAULT_CONFIG_PATH: &str = "/config/data/dhcpd-testing/default_config.json";

/// Returns `true` once the stream yields a `true`.
///
/// If the stream never yields `true` or never terminates, `try_any` may never resolve.
async fn try_any<S: Stream<Item = Result<bool>>>(stream: S) -> Result<bool> {
    futures::pin_mut!(stream);
    for v in stream.try_next().await.context("get next item")? {
        if v {
            return Ok(true);
        }
    }
    Ok(false)
}

/// Returns `true` if the stream only yields `true`.
///
/// If the stream never yields `false` or never terminates, `try_all` may never resolve.
async fn try_all<S: Stream<Item = Result<bool>>>(stream: S) -> Result<bool> {
    futures::pin_mut!(stream);
    for v in stream.try_next().await.context("get next item")? {
        if !v {
            return Ok(false);
        }
    }
    Ok(true)
}

trait EthertapName {
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
async fn sleep(secs: i64) {
    fasync::Timer::new(zx::Duration::from_seconds(secs).after_now()).await;
}

/// Waits for all addresses in `addrs` to be assigned to the interface
/// referenced by `interface_id` in `netstack`.
async fn wait_for_addresses(
    netstack: &fidl_fuchsia_netstack::NetstackProxy,
    interface_id: u64,
    addrs: impl Iterator<Item = fidl_fuchsia_net::IpAddress> + Clone,
) -> Result {
    let _ifaces = netstack
        .take_event_stream()
        .try_filter(|fidl_fuchsia_netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            futures::future::ready(
                interfaces
                    .iter()
                    .find(|iface| iface.id as u64 == interface_id)
                    .map(|iface| {
                        let iface_addrs = iface
                            .ipv6addrs
                            .iter()
                            .map(|a| &a.addr)
                            .chain(std::iter::once(&iface.addr));
                        addrs.clone().all(|want| iface_addrs.clone().any(|a| *a == want))
                    })
                    .unwrap_or(false),
            )
        })
        .try_next()
        .await
        .context("failed to observe IP Address")?
        .ok_or_else(|| anyhow::anyhow!("netstack event stream ended unexpectedly"))?;
    Ok(())
}
