// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for dropping connections

#![cfg(test)]

use super::{connect, Overnet};
use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_overnet::{
    ServiceConsumerProxyInterface, ServiceProviderRequest, ServicePublisherProxyInterface,
};
use fuchsia_zircon_status as zx_status;
use futures::prelude::*;
use overnet_core::NodeIdGenerator;
use std::sync::Arc;

#[fuchsia::test]
async fn drop_connection_2node(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("drop_connection_2node", run);
    // Two nodes, connected A->B.
    // Create a channel from A->B, then drop A.
    // See that the channel drops on B.
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    connect(&a, &b)?;
    run_drop_test(a, b).await
}

#[fuchsia::test]
#[ignore = "TODO(https://fxbug.dev/110501): Re-enable after CSO lands"]
#[fuchsia::test]
async fn drop_connection_3node(run: usize) -> Result<(), Error> {
    let mut node_id_gen = NodeIdGenerator::new("drop_connection_3node", run);
    // Three nodes, connected A->B->C.
    // Create a channel from A->C, then drop A.
    // See that the channel drops on C.
    let a = Overnet::new(&mut node_id_gen)?;
    let b = Overnet::new(&mut node_id_gen)?;
    let c = Overnet::new(&mut node_id_gen)?;
    connect(&a, &b)?;
    connect(&b, &c)?;
    run_drop_test(a, c).await
}

async fn run_drop_test(a: Arc<Overnet>, b: Arc<Overnet>) -> Result<(), Error> {
    futures::future::try_join(
        async move {
            let (cli, mut svr) = fidl::endpoints::create_request_stream()?;
            b.connect_as_service_publisher()?.publish_service("test", cli)?;
            let ServiceProviderRequest::ConnectToService {
                chan,
                info: _,
                control_handle: _control_handle,
            } = svr.try_next().await?.ok_or_else(|| format_err!("No test request received"))?;
            let chan = fidl::AsyncChannel::from_channel(chan)?;
            tracing::info!(node_id = b.node_id().0, "CLIENT CONNECTED TO SERVER");
            chan.write(&[], &mut vec![]).context("writing to client")?;
            tracing::info!(node_id = b.node_id().0, "WAITING FOR CLOSE");
            assert_eq!(
                chan.recv_msg(&mut Default::default()).await,
                Err(zx_status::Status::PEER_CLOSED)
            );
            Ok(())
        },
        async move {
            let chan = {
                let svc = a.connect_as_service_consumer()?;
                'retry: loop {
                    for mut peer in svc.list_peers().await? {
                        if peer
                            .description
                            .services
                            .unwrap_or(Vec::new())
                            .iter()
                            .find(|name| *name == "test")
                            .is_none()
                        {
                            continue;
                        }
                        let (s, p) = fidl::Channel::create()?;
                        svc.connect_to_service(&mut peer.id, "test", s)?;
                        break 'retry p;
                    }
                }
            };
            tracing::info!(node_id = a.node_id().0, "GOT CLIENT CHANNEL");
            let chan = fidl::AsyncChannel::from_channel(chan)?;
            chan.recv_msg(&mut Default::default()).await.context("waiting for server message")?;
            tracing::info!(node_id = a.node_id().0, "GOT MESSAGE FROM SERVER - DROPPING CLIENT");
            drop(a);
            drop(chan);
            Ok(())
        },
    )
    .map_ok(drop)
    .await
}
