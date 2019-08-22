// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_le::{PeripheralMarker, PeripheralProxy},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::emulator::LegacyAdvertisingState,
    },
    futures::{select, Future, FutureExt},
    pin_utils::pin_mut,
};

use crate::harness::{control::ActivatedFakeHost, TestHarness};

/// A snapshot of the current LE peripheral procedure states of the controller.
#[derive(Clone)]
pub struct PeripheralState {
    /// Observed changes to the controller's advertising state and parameters.
    pub advertising_state_changes: Vec<LegacyAdvertisingState>,
}

impl PeripheralState {
    /// Resets to the default state.
    pub fn reset(&mut self) {
        self.advertising_state_changes.clear();
    }
}

impl Default for PeripheralState {
    fn default() -> PeripheralState {
        PeripheralState { advertising_state_changes: Vec::new() }
    }
}

pub struct PeripheralHarnessAux {
    proxy: PeripheralProxy,
    emulator: HciEmulatorProxy,
}

impl PeripheralHarnessAux {
    fn new(proxy: PeripheralProxy, emulator: HciEmulatorProxy) -> PeripheralHarnessAux {
        PeripheralHarnessAux { proxy, emulator }
    }

    pub fn proxy(&self) -> &PeripheralProxy {
        &self.proxy
    }

    pub fn emulator(&self) -> &HciEmulatorProxy {
        &self.emulator
    }
}

pub type PeripheralHarness = ExpectationHarness<PeripheralState, PeripheralHarnessAux>;

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
    let watch = watch_advertising_state(state.clone());
    let run_test = test(state);

    // Run until one of the tasks finishes.
    pin_mut!(watch);
    pin_mut!(run_test);
    let result = select! {
        watch_result = watch.fuse() => watch_result,
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
