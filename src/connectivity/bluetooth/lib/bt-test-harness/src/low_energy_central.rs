// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_le::{CentralEvent, CentralMarker, CentralProxy},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::{
        expectation::asynchronous::{expectable, Expectable, ExpectableExt, ExpectableState},
        types::le::RemoteDevice,
    },
    futures::future::BoxFuture,
    futures::{FutureExt, TryStreamExt},
    std::{
        convert::TryInto,
        ops::{Deref, DerefMut},
    },
    test_harness::TestHarness,
};

use crate::deprecated::control::ActivatedFakeHost;

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

/// Auxilliary data for the CentralHarness
pub struct Aux {
    pub central: CentralProxy,
    emulator: HciEmulatorProxy,
}

impl AsRef<HciEmulatorProxy> for Aux {
    fn as_ref(&self) -> &HciEmulatorProxy {
        &self.emulator
    }
}

#[derive(Clone)]
pub struct CentralHarness(Expectable<CentralState, Aux>);

impl Deref for CentralHarness {
    type Target = Expectable<CentralState, Aux>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for CentralHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

impl TestHarness for CentralHarness {
    type Env = ActivatedFakeHost;
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let fake_host = ActivatedFakeHost::new("bt-hci-integration-le-0").await?;
            let central = fuchsia_component::client::connect_to_service::<CentralMarker>()
                .context("Failed to connect to BLE Central service")?;

            let harness = CentralHarness(expectable(
                Default::default(),
                Aux { central, emulator: fake_host.emulator().clone() },
            ));
            let run_central = handle_central_events(harness.clone()).boxed();
            Ok((harness, fake_host, run_central))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        env.release().boxed()
    }
}

async fn handle_central_events(harness: CentralHarness) -> Result<(), Error> {
    let mut events = harness.aux().central.take_event_stream();

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
