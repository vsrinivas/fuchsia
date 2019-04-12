// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_control::{
        AdapterInfo, ControlEvent, ControlMarker, ControlProxy, RemoteDevice,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectableStateExt, ExpectationHarness},
        expectation::Predicate,
        fake_hci::FakeHciDevice,
        util::{clone_host_info, clone_remote_device},
    },
    fuchsia_zircon::{Duration, DurationNum},
    futures::{Future, TryFutureExt, TryStreamExt},
    std::collections::HashMap,
};

use crate::harness::TestHarness;

/// Timeout for updating Bluetooth through fuchsia.bluetooth.control.Control actions
pub fn control_timeout() -> Duration {
    10.seconds()
}

pub struct ControlState {
    /// Current hosts
    pub hosts: HashMap<String, AdapterInfo>,
    /// Active host identifier
    pub active_host: Option<String>,
    /// Remote Peers seen
    pub peers: HashMap<String, RemoteDevice>,
}

impl Clone for ControlState {
    fn clone(&self) -> ControlState {
        let hosts: HashMap<String, AdapterInfo> =
            self.hosts.iter().map(|(k, v)| (k.clone(), clone_host_info(v))).collect();
        let active_host = self.active_host.clone();
        let peers: HashMap<String, RemoteDevice> =
            self.peers.iter().map(|(k, v)| (k.clone(), clone_remote_device(v))).collect();
        ControlState { hosts, active_host, peers }
    }
}

impl Default for ControlState {
    fn default() -> ControlState {
        let hosts = HashMap::new();
        let active_host = None;
        let peers = HashMap::new();
        ControlState { hosts, active_host, peers }
    }
}

pub type ControlHarness = ExpectationHarness<ControlState, ControlProxy>;

pub async fn handle_control_events(harness: ControlHarness) -> Result<(), Error> {
    let mut events = harness.aux().take_event_stream();

    while let Some(e) = await!(events.try_next())? {
        match e {
            ControlEvent::OnActiveAdapterChanged { adapter } => {
                harness.write_state().active_host = adapter.map(|host| host.identifier);
            }
            ControlEvent::OnAdapterUpdated { adapter } => {
                harness.write_state().hosts.insert(adapter.identifier.clone(), adapter);
            }
            ControlEvent::OnAdapterRemoved { identifier } => {
                harness.write_state().hosts.remove(&identifier);
            }
            ControlEvent::OnDeviceUpdated { device } => {
                harness.write_state().peers.insert(device.identifier.clone(), device);
            }
            ControlEvent::OnDeviceRemoved { identifier } => {
                harness.write_state().peers.remove(&identifier);
            }
        };
        harness.notify_state_changed();
    }
    Ok(())
}

pub async fn new_control_harness() -> Result<ControlHarness, Error> {
    let proxy = fuchsia_component::client::connect_to_service::<ControlMarker>()
        .context("Failed to connect to Control service")?;

    let control_harness = ControlHarness::new(proxy);

    // Store existing hosts in our state, as we won't get notified about them
    let hosts = await!(control_harness.aux().get_adapters())?;
    if let Some(hosts) = hosts {
        for host in hosts {
            control_harness.write_state().hosts.insert(host.identifier.clone(), host);
        }
    }

    fasync::spawn(
        handle_control_events(control_harness.clone())
            .unwrap_or_else(|e| eprintln!("Error handling control events: {:?}", e)),
    );

    Ok(control_harness)
}

/// Sets up the test environment and the given test case.
/// Each integration test case is asynchronous and must return a Future that completes with the
/// result of the test run.
pub async fn run_control_test_async<F, Fut>(test: F) -> Result<(), Error>
where
    F: FnOnce(ControlHarness) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let control_harness = await!(new_control_harness())?;

    await!(test(control_harness))
}

impl TestHarness for ControlHarness {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(run_control_test_async(test_func))
    }
}

pub mod control_expectation {
    use crate::harness::control::ControlState;
    use fuchsia_bluetooth::expectation::Predicate;

    pub fn active_host_is(id: String) -> Predicate<ControlState> {
        let msg = format!("active bt-host is {}", id);
        let expected_host = Some(id);
        Predicate::new(
            move |state: &ControlState| -> bool { state.active_host == expected_host },
            Some(&msg),
        )
    }

    pub fn host_not_present(id: String) -> Predicate<ControlState> {
        let msg = format!("bt-host {} is no longer present", id);
        Predicate::new(move |state: &ControlState| !state.hosts.contains_key(&id), Some(&msg))
    }
}

/// An activated fake host.
/// Must be released with `await!(host.release())` before drop.
pub struct ActivatedFakeHost {
    control: ControlHarness,
    host: String,
    hci: Option<FakeHciDevice>,
}

// All Fake HCI Devices have this address
pub const FAKE_HCI_ADDRESS: &'static str = "00:00:00:00:00:00";

impl ActivatedFakeHost {
    pub async fn new(name: &str) -> Result<ActivatedFakeHost, Error> {
        let control = await!(new_control_harness())?;
        let initial_hosts: Vec<String> = control.read().hosts.keys().cloned().collect();
        let initial_hosts_ = initial_hosts.clone();

        let hci = FakeHciDevice::new(name)?;

        let control_state = await!(control.when_satisfied(
            Predicate::<ControlState>::new(
                move |control| control
                    .hosts
                    .iter()
                    .any(|(id, host)| host.address == FAKE_HCI_ADDRESS
                        && !initial_hosts_.contains(id)),
                Some("At least one fake bt-host device added"),
            ),
            control_timeout()
        ))?;
        let host = control_state
            .hosts
            .iter()
            .find(|(id, host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts.contains(id))
            .unwrap()
            .1
            .identifier
            .to_string(); // We can safely unwrap here as this is guarded by the previous expectation
        await!(control.aux().set_active_adapter(&host))?;
        await!(control
            .when_satisfied(control_expectation::active_host_is(host.clone()), control_timeout()))?;
        Ok(ActivatedFakeHost { control, host, hci: Some(hci) })
    }

    pub async fn release(mut self) -> Result<(), Error> {
        self.hci = None;

        // Wait for BT-GAP to unregister the associated fake host
        await!(self.control.when_satisfied(
            control_expectation::host_not_present(self.host.clone()),
            control_timeout()
        ))?;
        Ok(())
    }
}

impl Drop for ActivatedFakeHost {
    fn drop(&mut self) {
        assert!(self.hci.is_none());
    }
}
