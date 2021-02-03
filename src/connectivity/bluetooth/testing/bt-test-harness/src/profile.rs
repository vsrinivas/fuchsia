// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::expectation::asynchronous::{expectable, Expectable, ExpectableExt},
    futures::future::BoxFuture,
    futures::{FutureExt, StreamExt},
    std::ops::{Deref, DerefMut},
    test_harness::TestHarness,
};

use crate::{deprecated::control::ActivatedFakeHost, emulator::EmulatorState};

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

/// Auxilliary data for the ProfileHarness
pub struct Aux {
    pub profile: ProfileProxy,
    pub emulator: HciEmulatorProxy,
}

#[derive(Clone)]
pub struct ProfileHarness(Expectable<ProfileState, Aux>);

impl Deref for ProfileHarness {
    type Target = Expectable<ProfileState, Aux>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for ProfileHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl TestHarness for ProfileHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let host = ActivatedFakeHost::new("bt-hci-integration-profile-0").await?;
            let profile = fuchsia_component::client::connect_to_service::<ProfileMarker>()
                .context("Failed to connect to Profile serivce")?;
            let harness = ProfileHarness(expectable(
                Default::default(),
                Aux { profile, emulator: host.emulator().clone() },
            ));

            let run_profile = handle_profile_events(harness.clone()).boxed();
            Ok((harness, host, run_profile))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

pub async fn handle_profile_events(harness: ProfileHarness) -> Result<(), Error> {
    let mut events = harness.aux().profile.take_event_stream();

    while let Some(evt) = events.next().await {
        let _ = evt?;
    }
    Ok(())
}
