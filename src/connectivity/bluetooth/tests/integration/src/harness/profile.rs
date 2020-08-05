// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::expectation::asynchronous::ExpectationHarness,
    futures::future::BoxFuture,
    futures::{FutureExt, StreamExt},
    parking_lot::MappedRwLockWriteGuard,
};

use crate::harness::{
    control::ActivatedFakeHost,
    emulator::{EmulatorHarness, EmulatorHarnessAux, EmulatorState},
    TestHarness,
};

#[derive(Clone, Debug, Default)]
pub struct ProfileState {
    emulator_state: EmulatorState,
}

impl std::convert::AsMut<EmulatorState> for ProfileState {
    fn as_mut(&mut self) -> &mut EmulatorState {
        &mut self.emulator_state
    }
}

impl std::convert::AsRef<EmulatorState> for ProfileState {
    fn as_ref(&self) -> &EmulatorState {
        &self.emulator_state
    }
}

pub type ProfileHarness = ExpectationHarness<ProfileState, ProfileHarnessAux>;
type ProfileHarnessAux = EmulatorHarnessAux<ProfileProxy>;

impl EmulatorHarness for ProfileHarness {
    type State = ProfileState;

    fn emulator(&self) -> HciEmulatorProxy {
        self.aux().emulator().clone()
    }

    fn state(&self) -> MappedRwLockWriteGuard<'_, ProfileState> {
        self.write_state()
    }
}

impl TestHarness for ProfileHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-profile-0").await?;
            let proxy = fuchsia_component::client::connect_to_service::<ProfileMarker>()
                .context("Failed to connect to Profile serivce")?;
            let harness =
                ProfileHarness::new(ProfileHarnessAux::new(proxy, fake_host.emulator().clone()));

            let run_profile = handle_profile_events(harness.clone()).boxed();
            Ok((harness, fake_host, run_profile))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

pub async fn handle_profile_events(harness: ProfileHarness) -> Result<(), Error> {
    let mut events = harness.aux().proxy().take_event_stream();

    while let Some(evt) = events.next().await {
        let _ = evt?;
    }
    Ok(())
}
