// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_le::{
        ConnectionProxy, PeripheralEvent, PeripheralMarker, PeripheralProxy,
    },
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::le::Peer,
    },
    futures::{select, Future, FutureExt, TryStreamExt},
    parking_lot::MappedRwLockWriteGuard,
    pin_utils::pin_mut,
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
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(run_peripheral_test_async(test_func))
    }
}

async fn run_peripheral_test_async<F, Fut>(test: F) -> Result<(), Error>
where
    F: FnOnce(PeripheralHarness) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    // Don't drop the ActivatedFakeHost until the end of this function.
    let host = ActivatedFakeHost::new("bt-integration-le-peripheral").await?;
    let proxy = fuchsia_component::client::connect_to_service::<PeripheralMarker>()
        .context("Failed to connect to BLE Peripheral service")?;
    let state = PeripheralHarness::new(PeripheralHarnessAux::new(proxy, host.emulator().clone()));

    // Initialize the state update watcher and the test run tasks.
    let watch_adv = watch_advertising_states(state.clone());
    let watch_conn = watch_connections(state.clone());
    let run_test = test(state);

    // Run until one of the tasks finishes.
    pin_mut!(watch_adv);
    pin_mut!(watch_conn);
    pin_mut!(run_test);
    let result = select! {
        watch_adv_result = watch_adv.fuse() => watch_adv_result,
        watch_conn_result = watch_conn.fuse() => watch_conn_result,
        test_result = run_test.fuse() => test_result,
    };

    host.release().await?;
    result
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
            // Ignore the deprecated events.
            PeripheralEvent::OnCentralConnected { .. } => (),
            PeripheralEvent::OnCentralDisconnected { .. } => (),
        }
        harness.notify_state_changed();
    }
    Ok(())
}
