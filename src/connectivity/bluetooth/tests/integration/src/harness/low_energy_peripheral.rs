// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_le::{
        ConnectionProxy, PeripheralEvent, PeripheralMarker, PeripheralProxy,
    },
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::le::Peer,
    },
    futures::future::{self, BoxFuture},
    futures::{FutureExt, TryFutureExt, TryStreamExt},
    parking_lot::MappedRwLockWriteGuard,
    std::convert::TryInto,
};

use crate::harness::{
    control::ActivatedFakeHost,
    emulator::{watch_advertising_states, EmulatorHarness, EmulatorHarnessAux, EmulatorState},
    TestHarness,
};

/// A snapshot of the current LE peripheral procedure states of the controller.
#[derive(Clone, Default)]
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

impl std::convert::AsMut<EmulatorState> for PeripheralState {
    fn as_mut(&mut self) -> &mut EmulatorState {
        &mut self.emulator_state
    }
}

impl std::convert::AsRef<EmulatorState> for PeripheralState {
    fn as_ref(&self) -> &EmulatorState {
        &self.emulator_state
    }
}

pub type PeripheralHarness = ExpectationHarness<PeripheralState, PeripheralHarnessAux>;
type PeripheralHarnessAux = EmulatorHarnessAux<PeripheralProxy>;

impl EmulatorHarness for PeripheralHarness {
    type State = PeripheralState;

    fn emulator(&self) -> HciEmulatorProxy {
        self.aux().emulator().clone()
    }

    fn state(&self) -> MappedRwLockWriteGuard<PeripheralState> {
        self.write_state()
    }
}

impl TestHarness for PeripheralHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            // Don't drop the ActivatedFakeHost until the end of this function.
            let host = ActivatedFakeHost::new("bt-integration-le-peripheral").await?;
            let proxy = fuchsia_component::client::connect_to_service::<PeripheralMarker>()
                .context("Failed to connect to BLE Peripheral service")?;
            let harness =
                PeripheralHarness::new(PeripheralHarnessAux::new(proxy, host.emulator().clone()));

            // Create a task to process the state update watcher
            let watch_adv = watch_advertising_states(harness.clone());
            let watch_conn = watch_connections(harness.clone());
            let run_peripheral = future::try_join(watch_adv, watch_conn).map_ok(|((), ())| ()).boxed();

            Ok((harness, host, run_peripheral))
        }
        .boxed()
    }
    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

async fn watch_connections(harness: PeripheralHarness) -> Result<(), Error> {
    let mut events = harness.aux().proxy().take_event_stream();
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
