// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    ffx_core::constants::MAX_RETRY_COUNT,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge::{DaemonMarker, DaemonProxy},
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
};

pub async fn create_daemon_proxy(id: &mut NodeId) -> Result<DaemonProxy, Error> {
    let svc = hoist::connect_as_service_consumer()?;
    let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
    svc.connect_to_service(id, DaemonMarker::NAME, s)?;
    let proxy = fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?;
    Ok(DaemonProxy::new(proxy))
}

// Note that this function assumes the daemon has been started separately.
pub async fn find_and_connect() -> Result<Option<DaemonProxy>, Error> {
    let svc = hoist::connect_as_service_consumer()?;
    // Sometimes list_peers doesn't properly report the published services - retry a few times
    // but don't loop indefinitely.
    for _ in 0..MAX_RETRY_COUNT {
        let peers = svc.list_peers().await?;
        for mut peer in peers {
            if peer.description.services.is_none() {
                continue;
            }
            if peer
                .description
                .services
                .unwrap()
                .iter()
                .find(|name| *name == DaemonMarker::NAME)
                .is_none()
            {
                continue;
            }
            return create_daemon_proxy(&mut peer.id).map(|r| r.map(|proxy| Some(proxy))).await;
        }
    }

    Ok(None)
}
