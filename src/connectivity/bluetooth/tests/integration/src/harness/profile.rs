// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy},
    fuchsia_bluetooth::expectation::asynchronous::ExpectationHarness,
    futures::future::BoxFuture,
    futures::{FutureExt, StreamExt},
};

use crate::harness::{control::ActivatedFakeHost, TestHarness};

impl TestHarness for ProfileHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-profile-0").await?;
            let proxy = fuchsia_component::client::connect_to_service::<ProfileMarker>()
                .context("Failed to connect to Profile serivce")?;
            let harness = ProfileHarness::new(proxy);

            let run_profile = handle_profile_events(harness.clone()).boxed();
            Ok((harness, fake_host, run_profile))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

pub type ProfileHarness = ExpectationHarness<(), ProfileProxy>;

pub async fn handle_profile_events(harness: ProfileHarness) -> Result<(), Error> {
    let mut events = harness.aux().take_event_stream();

    while let Some(evt) = events.next().await {
        let _ = evt?;
    }
    Ok(())
}
