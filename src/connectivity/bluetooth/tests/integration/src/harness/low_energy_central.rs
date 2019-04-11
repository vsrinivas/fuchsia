// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_le::{CentralEvent, CentralMarker, CentralProxy},
    fuchsia_app, fuchsia_async as fasync,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        le::RemoteDevice,
    },
    futures::{Future, TryFutureExt, TryStreamExt},
};

use crate::harness::{control::ActivatedFakeHost, TestHarness};

/// Sets up the test environment and the given test case.
/// Each integration test case is asynchronous and must return a Future that completes with the
/// result of the test run.
pub async fn run_central_test_async<F, Fut>(test: F) -> Result<(), Error>
where
    F: FnOnce(CentralHarness) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    // Don't drop the FakeHciDevice until the end of this function
    let fake_host = await!(ActivatedFakeHost::new("bt-hci-integration-le-0"))?;

    let proxy = fuchsia_app::client::connect_to_service::<CentralMarker>()
        .context("Failed to connect to BLE Central service")?;

    let state = CentralHarness::new(proxy.clone());
    fasync::spawn(
        handle_central_events(state.clone())
            .unwrap_or_else(|e| eprintln!("Error handling central events: {:?}", e)),
    );

    let result = await!(test(state));

    await!(fake_host.release())?;

    result
}

impl TestHarness for CentralHarness {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(run_central_test_async(test_func))
    }
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum ScanStateChange {
    ScanEnabled,
    ScanDisabled,
}

/// A snapshot of the current LowEnergy Central State
#[derive(Clone)]
pub struct CentralState {
    /// Observed scan state changes.
    pub scan_state_changes: Vec<ScanStateChange>,

    /// Discovered devices.
    pub remote_devices: Vec<RemoteDevice>,
}

impl Default for CentralState {
    fn default() -> CentralState {
        CentralState { scan_state_changes: Vec::new(), remote_devices: Vec::new() }
    }
}

pub type CentralHarness = ExpectationHarness<CentralState, CentralProxy>;

pub async fn handle_central_events(harness: CentralHarness) -> Result<(), Error> {
    let mut events = harness.aux().take_event_stream();

    while let Some(e) = await!(events.try_next())? {
        match e {
            CentralEvent::OnDeviceDiscovered { device } => {
                harness.write_state().remote_devices.push(device.into());
                harness.notify_state_changed();
            }
            CentralEvent::OnScanStateChanged { scanning } => {
                let change = if scanning {
                    ScanStateChange::ScanEnabled
                } else {
                    ScanStateChange::ScanDisabled
                };
                harness.write_state().scan_state_changes.push(change);
                harness.notify_state_changed();
            }
            CentralEvent::OnPeripheralDisconnected { identifier: _ } => {}
        };
    }
    Ok(())
}
