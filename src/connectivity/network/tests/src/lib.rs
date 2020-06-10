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
mod ipv6;
mod socket;

use std::fmt::Display;
use std::marker::Unpin;

use fidl_fuchsia_netstack as netstack;
use fuchsia_async::{DurationExt, TimeoutExt};
use fuchsia_zircon as zx;

use anyhow::Context as _;
use futures::future::{self, TryFutureExt};
use futures::stream::{FusedStream, Stream, TryStream, TryStreamExt};

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

/// The URL to NetCfg for use in a netemul environment.
///
/// Note, netcfg.cmx must never be used in a Netemul environment as it breaks hermeticity.
const NETCFG_PKG_URL: &str = "fuchsia-pkg://fuchsia.com/netcfg#meta/netcfg_netemul.cmx";

/// The path to the default configuration file for DHCP server.
const DHCP_SERVER_DEFAULT_CONFIG_PATH: &str = "/pkg/data/default_config.json";

/// Returns when an interface is up or when `timeout` elapses.
async fn wait_for_interface_up<
    S: Unpin + FusedStream + TryStreamExt<Ok = netstack::NetstackEvent>,
>(
    events: S,
    id: u64,
    timeout: zx::Duration,
) -> Result
where
    <S as TryStream>::Error: Display,
{
    events
        .try_filter_map(|netstack::NetstackEvent::OnInterfacesChanged { interfaces }| {
            if let Some(netstack::NetInterface { flags, .. }) =
                interfaces.iter().find(|i| u64::from(i.id) == id)
            {
                if flags & netstack::NET_INTERFACE_FLAG_UP != 0 {
                    return future::ok(Some(()));
                }
            }

            future::ok(None)
        })
        .try_next()
        .map_err(|e| anyhow::anyhow!("error getting OnInterfaceChanged event: {}", e))
        .on_timeout(timeout.after_now(), || {
            Err(anyhow::anyhow!("timed out waiting for interface up event"))
        })
        .await?
        .ok_or(anyhow::anyhow!("failed to get next OnInterfaceChanged event"))
}

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
