// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::generator::Generator,
    crate::probe_node::{probe_node, Selector},
    anyhow::Error,
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    std::collections::HashSet,
};

// List peers, but wait for things to settle out first
pub fn list_peers() -> impl Stream<Item = Result<NodeId, Error>> {
    Generator::new(move |mut tx| async move {
        let r: Result<(), Error> = async {
            let svc = hoist::connect_as_service_consumer()?;
            let mut seen_peers = HashSet::new();
            // Track peers that we expect to see via svc.list_peers() but have not yet.
            // Once seen_peers is populated and unseen_peers is empty we can be reasonably sure
            // that we've captured a consistent snapshot of the mesh.
            let mut unseen_peers = HashSet::new();
            while !unseen_peers.is_empty() || seen_peers.is_empty() {
                for peer in svc.list_peers().await?.into_iter() {
                    unseen_peers.remove(&peer.id.id);
                    if !seen_peers.insert(peer.id.id) {
                        continue;
                    }
                    // check links on this node to see if there's any other nodes that we should know about.
                    for link in
                        probe_node(peer.id, Selector::Links).await?.links.unwrap_or_else(Vec::new)
                    {
                        if let Some(destination) = link.destination {
                            if !seen_peers.contains(&destination.id) {
                                unseen_peers.insert(destination.id);
                            }
                        }
                    }
                    tx.send(Ok(peer.id)).await?;
                }
            }
            Ok(())
        }
        .await;
        if let Err(e) = r {
            let _ = tx.send(Err(e));
        }
    })
}

/// Get this nodes id
pub async fn own_id() -> Result<NodeId, Error> {
    for peer in hoist::connect_as_service_consumer()?.list_peers().await?.into_iter() {
        if peer.is_self {
            return Ok(peer.id);
        }
    }
    Err(anyhow::format_err!("Cannot find myself"))
}
