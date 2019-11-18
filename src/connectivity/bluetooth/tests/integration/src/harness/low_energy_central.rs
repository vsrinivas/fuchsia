// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_le::{CentralEvent, CentralMarker, CentralProxy},
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectationHarness},
        types::le::RemoteDevice,
    },
    futures::future::BoxFuture,
    futures::{FutureExt, TryStreamExt},
    std::convert::TryInto,
};

use crate::harness::{control::ActivatedFakeHost, emulator::EmulatorHarnessAux, TestHarness};

impl TestHarness for CentralHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            // Don't drop the ActivatedFakeHost until terminate()
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-le-0").await?;
            let proxy = fuchsia_component::client::connect_to_service::<CentralMarker>()
                .context("Failed to connect to BLE Central service")?;

            let harness =
                CentralHarness::new(CentralHarnessAux::new(proxy, fake_host.emulator().clone()));
            let run_central = handle_central_events(harness.clone()).boxed();
            Ok((harness, fake_host, run_central))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

#[derive(PartialEq, Debug, Clone, Copy)]
pub enum ScanStateChange {
    ScanEnabled,
    ScanDisabled,
}

/// A snapshot of the current LowEnergy Central State
#[derive(Clone, Default)]
pub struct CentralState {
    /// Observed scan state changes.
    pub scan_state_changes: Vec<ScanStateChange>,

    /// Discovered devices.
    pub remote_devices: Vec<RemoteDevice>,
}

pub type CentralHarness = ExpectationHarness<CentralState, CentralHarnessAux>;
type CentralHarnessAux = EmulatorHarnessAux<CentralProxy>;

async fn handle_central_events(harness: CentralHarness) -> Result<(), Error> {
    let mut events = harness.aux().proxy().take_event_stream();

    while let Some(e) = events.try_next().await? {
        match e {
            CentralEvent::OnDeviceDiscovered { device } => {
                harness.write_state().remote_devices.push(device.try_into()?);
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
