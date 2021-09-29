// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_bluetooth_sys::{HostWatcherMarker, HostWatcherProxy},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_bluetooth::{
        expectation::asynchronous::{
            expectable, Expectable, ExpectableExt, ExpectableState, ExpectableStateExt,
        },
        expectation::Predicate,
        types::{Address, HostId, HostInfo},
    },
    fuchsia_zircon as zx,
    futures::future::{self, BoxFuture, FutureExt, TryFutureExt},
    hci_emulator_client::Emulator,
    std::{
        collections::HashMap,
        convert::TryFrom,
        ops::{Deref, DerefMut},
        sync::Arc,
    },
    test_harness::{SharedState, TestHarness},
    tracing::error,
};

use crate::timeout_duration;

#[derive(Clone, Default)]
pub struct HostWatcherState {
    /// Current hosts
    pub hosts: HashMap<HostId, HostInfo>,
}

#[derive(Clone)]
pub struct HostWatcherHarness(Expectable<HostWatcherState, HostWatcherProxy>);

impl Deref for HostWatcherHarness {
    type Target = Expectable<HostWatcherState, HostWatcherProxy>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for HostWatcherHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

async fn watch_hosts(harness: HostWatcherHarness) -> Result<(), Error> {
    let proxy = harness.aux().clone();
    loop {
        let hosts = proxy
            .watch()
            .await
            .context("Error calling fuchsia.bluetooth.sys.HostWatcher.watch()")?;
        let hosts: Result<HashMap<HostId, HostInfo>, Error> = hosts
            .into_iter()
            .map(|info| {
                let info = HostInfo::try_from(info);
                info.map(|info| (info.id, info))
            })
            .collect();
        let hosts = hosts
            .context("Invalid host received from fuchsia.bluetooth.sys.HostWatcher.watch()")?;
        harness.write_state().hosts = hosts;
        harness.notify_state_changed();
    }
}

pub async fn new_host_watcher_harness() -> Result<HostWatcherHarness, Error> {
    let proxy = fuchsia_component::client::connect_to_protocol::<HostWatcherMarker>()
        .context("Failed to connect to host_watcher service")?;

    Ok(HostWatcherHarness(expectable(Default::default(), proxy)))
}

impl TestHarness for HostWatcherHarness {
    type Env = ();
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init(
        _shared_state: &Arc<SharedState>,
    ) -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let harness = new_host_watcher_harness().await?;
            let run_host_watcher = watch_hosts(harness.clone())
                .map_err(|e| e.context("Error running HostWatcher harness"))
                .boxed();
            Ok((harness, (), run_host_watcher))
        }
        .boxed()
    }
    fn terminate(_env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        future::ok(()).boxed()
    }
}

/// An activated fake host.
/// Must be released with `host.release().await` before drop.
pub struct ActivatedFakeHost {
    host_watcher: HostWatcherHarness,
    host: HostId,
    hci: Option<Emulator>,
}

// All Fake HCI Devices have this address
pub const FAKE_HCI_ADDRESS: Address = Address::Public([0, 0, 0, 0, 0, 0]);

/// Create and publish an emulated HCI device, and wait until the host is bound and registered to
/// bt-gap
pub async fn activate_fake_host(
    host_watcher: HostWatcherHarness,
) -> Result<(HostId, Emulator), Error> {
    let initial_hosts: Vec<HostId> = host_watcher.read().hosts.keys().cloned().collect();
    let initial_hosts_ = initial_hosts.clone();

    let hci = Emulator::create_and_publish().await?;

    let host_watcher_state = host_watcher
        .when_satisfied(
            expectation::host_exists(Predicate::predicate(
                move |host: &HostInfo| {
                    host.address == FAKE_HCI_ADDRESS && !initial_hosts_.contains(&host.id)
                },
                &format!("A fake host that is not in {:?}", initial_hosts),
            )),
            timeout_duration(),
        )
        .await?;

    let host = host_watcher_state
        .hosts
        .iter()
        .find(|(id, host)| host.address == FAKE_HCI_ADDRESS && !initial_hosts.contains(id))
        .unwrap()
        .1
        .id; // We can safely unwrap here as this is guarded by the previous expectation

    let fut = host_watcher.aux().set_active(&mut host.into());
    fut.await?
        .or_else(zx::Status::ok)
        .map_err(|e| format_err!("failed to set active host to emulator: {}", e))?;

    let _ = host_watcher
        .when_satisfied(expectation::active_host_is(host.clone()), timeout_duration())
        .await?;
    Ok((host, hci))
}

impl ActivatedFakeHost {
    pub async fn new() -> Result<ActivatedFakeHost, Error> {
        let host_watcher = new_host_watcher_harness().await?;
        fuchsia_async::Task::spawn(
            watch_hosts(host_watcher.clone())
                .unwrap_or_else(|e| error!("Error watching hosts: {:?}", e)),
        )
        .detach();
        let (host, hci) = activate_fake_host(host_watcher.clone()).await?;
        Ok(ActivatedFakeHost { host_watcher, host, hci: Some(hci) })
    }

    pub async fn release(mut self) -> Result<(), Error> {
        // Wait for the test device to be destroyed.
        if let Some(hci) = &mut self.hci {
            hci.destroy_and_wait().await?;
        }
        self.hci = None;

        // Wait for BT-GAP to unregister the associated fake host
        let _ = self
            .host_watcher
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

pub mod expectation {
    use super::*;
    use fuchsia_bluetooth::{
        expectation::Predicate,
        types::{HostId, HostInfo},
    };

    pub(crate) fn host_not_present(id: HostId) -> Predicate<HostWatcherState> {
        let msg = format!("Host not present: {}", id);
        Predicate::predicate(move |state: &HostWatcherState| !state.hosts.contains_key(&id), &msg)
    }

    pub(crate) fn host_exists(p: Predicate<HostInfo>) -> Predicate<HostWatcherState> {
        let msg = format!("Host exists satisfying {:?}", p);
        Predicate::predicate(
            move |state: &HostWatcherState| state.hosts.iter().any(|(_, host)| p.satisfied(&host)),
            &msg,
        )
    }

    pub(crate) fn active_host_is(id: HostId) -> Predicate<HostWatcherState> {
        let msg = format!("Active host is: {}", id);
        Predicate::predicate(
            move |state: &HostWatcherState| {
                state.hosts.get(&id).and_then(|h| Some(h.active)) == Some(true)
            },
            &msg,
        )
    }
}
