// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_le::{
        ConnectionProxy, PeripheralEvent, PeripheralMarker, PeripheralProxy,
    },
    fidl_fuchsia_bluetooth_test::{ConnectionState, PeerProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::emulator::LegacyAdvertisingState,
        types::{le::Peer, Address},
    },
    futures::{select, Future, FutureExt, TryStreamExt},
    pin_utils::pin_mut,
    std::collections::HashMap,
};

use crate::harness::{control::ActivatedFakeHost, EmulatorHarnessAux, TestHarness};

/// A snapshot of the current LE peripheral procedure states of the controller.
#[derive(Clone)]
pub struct PeripheralState {
    /// Observed changes to the controller's advertising state and parameters.
    pub advertising_state_changes: Vec<LegacyAdvertisingState>,

    /// List of observed peer connection states.
    pub connection_states: HashMap<Address, Vec<ConnectionState>>,

    /// Observed peer connections.
    pub connections: Vec<(Peer, ConnectionProxy)>,
}

impl PeripheralState {
    /// Resets to the default state.
    pub fn reset(&mut self) {
        self.advertising_state_changes.clear();
        self.connection_states.clear();
        self.connections.clear();
    }
}

impl Default for PeripheralState {
    fn default() -> PeripheralState {
        PeripheralState {
            advertising_state_changes: Vec::new(),
            connection_states: HashMap::new(),
            connections: Vec::new(),
        }
    }
}

pub type PeripheralHarness = ExpectationHarness<PeripheralState, PeripheralHarnessAux>;
type PeripheralHarnessAux = EmulatorHarnessAux<PeripheralProxy>;

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
    let state =
        PeripheralHarness::new(PeripheralHarnessAux { proxy, emulator: host.emulator().clone() });

    // Initialize the state update watcher and the test run tasks.
    let watch_adv = watch_advertising_state(state.clone());
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

async fn watch_advertising_state(harness: PeripheralHarness) -> Result<(), Error> {
    loop {
        let states = harness.aux().emulator().watch_legacy_advertising_states().await?;
        harness
            .write_state()
            .advertising_state_changes
            .append(&mut states.into_iter().map(|s| s.into()).collect());
        harness.notify_state_changed();
    }
}

async fn watch_connections(harness: PeripheralHarness) -> Result<(), Error> {
    let mut events = harness.aux().proxy().take_event_stream();
    while let Some(e) = events.try_next().await? {
        match e {
            PeripheralEvent::OnPeerConnected { peer, connection } => {
                harness.write_state().connections.push((peer.into(), connection.into_proxy()?));
            }
            // Ignore the deprecated events.
            PeripheralEvent::OnCentralConnected { .. } => (),
            PeripheralEvent::OnCentralDisconnected { .. } => (),
        }
        harness.notify_state_changed();
    }
    Ok(())
}

/// Process connection state events for a particular peer over the given `proxy`.
pub async fn watch_emulator_peer_connection_states(
    harness: PeripheralHarness,
    address: Address,
    proxy: PeerProxy,
) -> Result<(), Error> {
    loop {
        let mut result = proxy.watch_connection_states().await?;
        // Introduce a scope as it is important not to hold a mutable lock to the harness state when
        // we call `harness.notify_state_changed()` below.
        {
            let state_map = &mut harness.write_state().connection_states;
            if !state_map.contains_key(&address) {
                state_map.insert(address, vec![]);
            }
            let states = state_map.get_mut(&address).unwrap();
            states.append(&mut result);
        }
        harness.notify_state_changed();
    }
}
