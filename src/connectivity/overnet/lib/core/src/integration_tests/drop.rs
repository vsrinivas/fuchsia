// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Tests for dropping connections

#![cfg(test)]

use super::{connect, Overnet};
use crate::router::test_util::{run, run_repeatedly};
use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_overnet::{
    ServiceConsumerProxyInterface, ServiceProviderRequest, ServicePublisherProxyInterface,
};
use fuchsia_zircon_status as zx_status;
use futures::prelude::*;
use std::sync::Arc;

#[test]
fn drop_connection_2node() -> Result<(), Error> {
    run(async move {
        // Two nodes, connected A->B.
        // Create a channel from A->B, then drop A.
        // See that the channel drops on B.
        let a = Overnet::new(888.into())?;
        let b = Overnet::new(999.into())?;
        connect(&a, &b)?;
        run_drop_test(a, b).await
    })
}

#[test]
fn drop_connection_3node() -> Result<(), Error> {
    #[cfg(target_os = "fuchsia")]
    const RUN_COUNT: u64 = 1;
    #[cfg(not(target_os = "fuchsia"))]
    const RUN_COUNT: u64 = 1;
    run_repeatedly(RUN_COUNT, |i| async move {
        // Three nodes, connected A->B->C.
        // Create a channel from A->C, then drop A.
        // See that the channel drops on C.
        let a = Overnet::new((100000 * i + 90011).into())?;
        let b = Overnet::new((100000 * i + 90022).into())?;
        let c = Overnet::new((100000 * i + 90033).into())?;
        connect(&a, &b)?;
        connect(&b, &c)?;
        run_drop_test(a, c).await
    })
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
            eprintln!("{:?} CLIENT CONNECTED TO SERVER", b.node_id());
            chan.write(&[], &mut vec![]).context("writing to client")?;
            eprintln!("{:?} WAITING FOR CLOSE", b.node_id());
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
            eprintln!("{:?} GOT CLIENT CHANNEL", a.node_id());
            let chan = fidl::AsyncChannel::from_channel(chan)?;
            chan.recv_msg(&mut Default::default()).await.context("waiting for server message")?;
            eprintln!("{:?} GOT MESSAGE FROM SERVER - DROPPING CLIENT", a.node_id());
            drop(a);
            drop(chan);
            Ok(())
        },
    )
    .map_ok(drop)
    .await
}
