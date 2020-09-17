// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_control::{
        AdapterInfo, ControlEvent, ControlMarker, ControlProxy, RemoteDevice,
    },
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        expectation::asynchronous::{ExpectableState, ExpectableStateExt, ExpectationHarness},
        expectation::Predicate,
        hci_emulator::Emulator,
        util::{clone_host_info, clone_remote_device},
    },
    futures::{
        future::{self, BoxFuture},
        FutureExt, TryFutureExt, TryStreamExt,
    },
    log::error,
    std::collections::HashMap,
};

use crate::{harness::TestHarness, tests::timeout_duration};

#[derive(Debug, Default)]
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

pub type ControlHarness = ExpectationHarness<ControlState, ControlProxy>;

pub async fn handle_control_events(harness: ControlHarness) -> Result<(), Error> {
    let mut events = harness.aux().take_event_stream();
    while let Some(e) = events
        .try_next()
        .await
        .context("Error receiving fuchsia.bluetooth.control.Control events")?
    {
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
    let fut = control_harness.aux().get_adapters();
    let hosts = fut.await?;
    if let Some(hosts) = hosts {
        for host in hosts {
            control_harness.write_state().hosts.insert(host.identifier.clone(), host);
        }
    }
    Ok(control_harness)
}

impl TestHarness for ControlHarness {
    type Env = ();
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let harness = new_control_harness().await?;
            let run_control = handle_control_events(harness.clone()).boxed();
            Ok((harness, (), run_control))
        }
        .boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}

pub mod expectation {
    use crate::harness::control::ControlState;
    use fidl_fuchsia_bluetooth_control::RemoteDevice;
    use fuchsia_bluetooth::expectation::Predicate;

    pub fn active_host_is(id: String) -> Predicate<ControlState> {
        let msg = format!("active bt-host is {}", id);
        let expected_host = Some(id);
        Predicate::predicate(
            move |state: &ControlState| -> bool { state.active_host == expected_host },
            msg,
        )
    }

    pub fn host_not_present(id: String) -> Predicate<ControlState> {
        let msg = format!("bt-host {} is no longer present", id);
        Predicate::predicate(move |state: &ControlState| !state.hosts.contains_key(&id), msg)
    }

    mod peer {
        use super::*;

        pub(crate) fn exists(p: Predicate<RemoteDevice>) -> Predicate<ControlState> {
            let msg = format!("peer exists satisfying {:?}", p);
            Predicate::predicate(
                move |state: &ControlState| state.peers.iter().any(|(_, d)| p.satisfied(d)),
                &msg,
            )
        }

        pub(crate) fn with_identifier(id: &str) -> Predicate<RemoteDevice> {
            let id_owned = id.to_string();
            Predicate::<RemoteDevice>::predicate(
                move |d| d.identifier == id_owned,
                &format!("identifier == {}", id),
            )
        }

        pub(crate) fn with_address(address: &str) -> Predicate<RemoteDevice> {
            let addr_owned = address.to_string();
            Predicate::<RemoteDevice>::predicate(
                move |d| d.address == addr_owned,
                &format!("address == {}", address),
            )
        }

        pub(crate) fn connected(connected: bool) -> Predicate<RemoteDevice> {
            Predicate::<RemoteDevice>::predicate(
                move |d| d.connected == connected,
                &format!("connected == {}", connected),
            )
        }
    }

    pub fn peer_connected(id: &str, connected: bool) -> Predicate<ControlState> {
        peer::exists(peer::with_identifier(id).and(peer::connected(connected)))
    }

    pub fn peer_with_address(address: &str) -> Predicate<ControlState> {
        peer::exists(peer::with_address(address))
    }
}

/// An activated fake host.
/// Must be released with `host.release().await` before drop.
pub struct ActivatedFakeHost {
    control: ControlHarness,
    host: String,
    hci: Option<Emulator>,
}

// All Fake HCI Devices have this address
pub const FAKE_HCI_ADDRESS: &'static str = "00:00:00:00:00:00";

/// Create and publish an emulated HCI device, and wait until the host is bound and registered to
/// bt-gap
pub async fn activate_fake_host(
    control: ControlHarness,
    name: &str,
) -> Result<(String, Emulator), Error> {
    let initial_hosts: Vec<String> = control.read().hosts.keys().cloned().collect();
    let initial_hosts_ = initial_hosts.clone();

    let hci = Emulator::create_and_publish(name).await?;

    let control_state = control
        .when_satisfied(
            Predicate::<ControlState>::predicate(
                move |control| {
                    control.hosts.iter().any(|(id, host)| {
                        host.address == FAKE_HCI_ADDRESS && !initial_hosts_.contains(id)
                    })
                },
                "At least one fake bt-host device added",
            ),
            timeout_duration(),
        )
        .await?;

    let host = control_state
        .hosts
        .iter()
        .find(|(id, host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts.contains(id))
        .unwrap()
        .1
        .identifier
        .to_string(); // We can safely unwrap here as this is guarded by the previous expectation

    let fut = control.aux().set_active_adapter(&host);
    fut.await?;
    control.when_satisfied(expectation::active_host_is(host.clone()), timeout_duration()).await?;
    Ok((host, hci))
}

impl ActivatedFakeHost {
    pub async fn new(name: &str) -> Result<ActivatedFakeHost, Error> {
        let control = new_control_harness().await?;
        fasync::Task::spawn(
            handle_control_events(control.clone())
                .unwrap_or_else(|e| error!("Error handling control events: {:?}", e)),
        )
        .detach();
        let (host, hci) = activate_fake_host(control.clone(), name).await?;
        Ok(ActivatedFakeHost { control, host, hci: Some(hci) })
    }

    pub async fn release(mut self) -> Result<(), Error> {
        // Wait for the test device to be destroyed.
        if let Some(hci) = &mut self.hci {
            hci.destroy_and_wait().await?;
        }
        self.hci = None;

        // Wait for BT-GAP to unregister the associated fake host
        self.control
            .when_satisfied(expectation::host_not_present(self.host.clone()), timeout_duration())
            .await?;
        Ok(())
    }

    pub fn emulator(&self) -> &HciEmulatorProxy {
        self.hci.as_ref().expect("emulator proxy requested after shut down").emulator()
    }
}

impl Drop for ActivatedFakeHost {
    fn drop(&mut self) {
        assert!(self.hci.is_none());
    }
}
