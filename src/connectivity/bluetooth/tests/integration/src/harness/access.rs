// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_sys::{AccessMarker, AccessProxy},
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::{Peer, PeerId},
    },
    futures::future::{self, BoxFuture, FutureExt},
    std::{collections::HashMap, convert::TryInto},
};

use crate::harness::TestHarness;

#[derive(Clone, Default)]
pub struct AccessState {
    /// Remote Peers seen
    pub peers: HashMap<PeerId, Peer>,
}

pub type AccessHarness = ExpectationHarness<AccessState, AccessProxy>;

async fn watch_peers(harness: AccessHarness) -> Result<(), Error> {
    let proxy = harness.aux().clone();
    loop {
        let (updated, removed) = proxy.watch_peers().await?;
        for peer in updated.into_iter() {
            let peer: Peer = peer.try_into().context("Invalid peer received from WatchPeers()")?;
            harness.write_state().peers.insert(peer.id, peer);
        }
        for id in removed.into_iter() {
            harness.write_state().peers.remove(&id.into());
        }
        harness.notify_state_changed();
    }
}

pub async fn new_access_harness() -> Result<AccessHarness, Error> {
    let proxy = fuchsia_component::client::connect_to_service::<AccessMarker>()
        .context("Failed to connect to access service")?;

    Ok(AccessHarness::new(proxy))
}

impl TestHarness for AccessHarness {
    type Env = ();
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let harness = new_access_harness().await?;
            let run_access = watch_peers(harness.clone()).boxed();
            Ok((harness, (), run_access))
        }
        .boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}
