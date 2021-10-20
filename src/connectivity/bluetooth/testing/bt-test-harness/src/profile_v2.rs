// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_bredr::{ProfileMarker, ProfileProxy},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::expectation::asynchronous::{expectable, Expectable, ExpectableExt},
    futures::future::BoxFuture,
    futures::{FutureExt, StreamExt},
    std::{
        ops::{Deref, DerefMut},
        sync::Arc,
    },
    test_harness::{SharedState, TestHarness},
};

use crate::{
    core_realm::{CoreRealm, SHARED_STATE_INDEX},
    emulator::EmulatorState,
    host_watcher_v2::ActivatedFakeHost,
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
    type Env = (ActivatedFakeHost, Arc<CoreRealm>);
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init(
        shared_state: &Arc<SharedState>,
    ) -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        let shared_state = shared_state.clone();
        async move {
            let realm =
                shared_state.get_or_insert_with(SHARED_STATE_INDEX, CoreRealm::create).await?;
            let host = ActivatedFakeHost::new(realm.clone()).await?;
            let profile = realm
                .instance()
                .connect_to_protocol_at_exposed_dir::<ProfileMarker>()
                .context("failed to connect to Profile service")?;
            let harness = ProfileHarness(expectable(
                Default::default(),
                Aux { profile, emulator: host.emulator().clone() },
            ));

            let run_profile = handle_profile_events(harness.clone()).boxed();
            Ok((harness, (host, realm), run_profile))
        }
        .boxed()
    }

    fn terminate((emulator, realm): Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        // The realm must be kept alive in order for emulator.release() to work properly.
        async move {
            let _realm = realm;
            emulator.release().await
        }
        .boxed()
    }
}

pub async fn handle_profile_events(harness: ProfileHarness) -> Result<(), Error> {
    let mut events = harness.aux().profile.take_event_stream();

    while let Some(evt) = events.next().await {
        let _ = evt?;
    }
    Ok(())
}
