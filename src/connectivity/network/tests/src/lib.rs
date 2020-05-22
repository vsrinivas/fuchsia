// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

mod constants;
mod dhcp;
mod dns;
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

use anyhow;
use futures::future::{self, TryFutureExt};
use futures::stream::{FusedStream, TryStream, TryStreamExt};

type Result<T = ()> = std::result::Result<T, anyhow::Error>;

/// Default timeout to use when waiting for an interface to come up.
const DEFAULT_INTERFACE_UP_EVENT_TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);

/// Returns when an interface is up or `INTERFACE_UP_EVENT_TIMEOUT` time units have passed.
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
