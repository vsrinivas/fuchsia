// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_le::{
        ConnectionProxy, PeripheralEvent, PeripheralMarker, PeripheralProxy,
    },
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::{
        expectation::asynchronous::{expectable, Expectable, ExpectableExt, ExpectableState},
        types::le::Peer,
    },
    futures::future::{self, BoxFuture},
    futures::{FutureExt, TryFutureExt, TryStreamExt},
    std::{
        convert::TryInto,
        ops::{Deref, DerefMut},
    },
    test_harness::TestHarness,
};

use crate::{
    deprecated::control::ActivatedFakeHost,
    emulator::{watch_advertising_states, EmulatorState},
};

/// A snapshot of the current LE peripheral procedure states of the controller.
#[derive(Clone, Debug, Default)]
pub struct PeripheralState {
    emulator_state: EmulatorState,

    /// Observed peer connections.
    pub connections: Vec<(Peer, ConnectionProxy)>,
}

impl PeripheralState {
    /// Resets to the default state.
    pub fn reset(&mut self) {
        self.emulator_state = EmulatorState::default();
        self.connections.clear();
    }
}

impl AsMut<EmulatorState> for PeripheralState {
    fn as_mut(&mut self) -> &mut EmulatorState {
        &mut self.emulator_state
    }
}

impl AsRef<EmulatorState> for PeripheralState {
    fn as_ref(&self) -> &EmulatorState {
        &self.emulator_state
    }
}

#[derive(Clone)]
pub struct PeripheralHarness(Expectable<PeripheralState, Aux>);

impl Deref for PeripheralHarness {
    type Target = Expectable<PeripheralState, Aux>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for PeripheralHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

/// Auxilliary data for the PeripheralHarness
pub struct Aux {
    pub peripheral: PeripheralProxy,
    emulator: HciEmulatorProxy,
}

impl AsRef<HciEmulatorProxy> for Aux {
    fn as_ref(&self) -> &HciEmulatorProxy {
        &self.emulator
    }
}

impl TestHarness for PeripheralHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let host = ActivatedFakeHost::new("bt-integration-le-peripheral").await?;
            let peripheral = fuchsia_component::client::connect_to_service::<PeripheralMarker>()
                .context("Failed to connect to BLE Peripheral service")?;
            let harness = PeripheralHarness(expectable(
                Default::default(),
                Aux { peripheral, emulator: host.emulator().clone() },
            ));

            // Create a task to process the state update watcher
            let watch_adv = watch_advertising_states(harness.deref().clone());
            let watch_conn = watch_connections(harness.clone());
            let run_peripheral =
                future::try_join(watch_adv, watch_conn).map_ok(|((), ())| ()).boxed();

            Ok((harness, host, run_peripheral))
        }
        .boxed()
    }
    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

async fn watch_connections(harness: PeripheralHarness) -> Result<(), Error> {
    let mut events = harness.aux().peripheral.take_event_stream();
    while let Some(e) = events.try_next().await? {
        match e {
            PeripheralEvent::OnPeerConnected { peer, connection } => {
                harness
                    .write_state()
                    .connections
                    .push((peer.try_into()?, connection.into_proxy()?));
            }
        }
        harness.notify_state_changed();
    }
    Ok(())
}
