// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_sys::{HostWatcherMarker, HostWatcherProxy},
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::{HostId, HostInfo},
    },
    futures::future::{self, BoxFuture, FutureExt},
    std::{collections::HashMap, convert::TryFrom},
};

use crate::harness::TestHarness;

#[derive(Clone, Default)]
pub struct HostWatcherState {
    /// Current hosts
    pub hosts: HashMap<HostId, HostInfo>,
}

pub type HostWatcherHarness = ExpectationHarness<HostWatcherState, HostWatcherProxy>;

async fn watch_hosts(harness: HostWatcherHarness) -> Result<(), Error> {
    let proxy = harness.aux().clone();
    loop {
        let hosts = proxy.watch().await?;
        let hosts: Result<HashMap<HostId, HostInfo>, Error> = hosts
            .into_iter()
            .map(|info| {
                let info = HostInfo::try_from(info);
                info.map(|info| (info.id, info))
            })
            .collect();
        let hosts = hosts?;
        harness.write_state().hosts = hosts;
        harness.notify_state_changed();
    }
}

pub async fn new_host_watcher_harness() -> Result<HostWatcherHarness, Error> {
    let proxy = fuchsia_component::client::connect_to_service::<HostWatcherMarker>()
        .context("Failed to connect to host_watcher service")?;

    Ok(HostWatcherHarness::new(proxy))
}

impl TestHarness for HostWatcherHarness {
    type Env = ();
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let harness = new_host_watcher_harness().await?;
            let run_host_watcher = watch_hosts(harness.clone()).boxed();
            Ok((harness, (), run_host_watcher))
        }
        .boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}
