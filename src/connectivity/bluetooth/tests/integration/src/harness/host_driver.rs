// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        constants::HOST_DEVICE_DIR,
        device_watcher::DeviceWatcher,
        expectation::{
            asynchronous::{ExpectableState, ExpectableStateExt, ExpectationHarness},
            Predicate,
        },
        hci_emulator::Emulator,
        host,
        types::{HostInfo, Peer, PeerId},
    },
    fuchsia_zircon::{Duration, DurationNum},
    futures::{
        future::{self, BoxFuture},
        FutureExt, TryFutureExt,
    },
    parking_lot::MappedRwLockWriteGuard,
    std::{
        collections::HashMap,
        convert::{AsMut, AsRef, TryInto},
        path::PathBuf,
    },
};

use crate::harness::{
    emulator::{watch_controller_parameters, EmulatorHarness, EmulatorHarnessAux, EmulatorState},
    TestHarness,
};

const TIMEOUT_SECONDS: i64 = 10; // in seconds

pub fn timeout_duration() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

// Returns a Future that resolves when the state of any RemoteDevice matches `target`.
pub async fn expect_peer(
    host: &HostDriverHarness,
    target: Predicate<Peer>,
) -> Result<HostState, Error> {
    let fut = host.when_satisfied(
        Predicate::<HostState>::new(
            move |host| host.peers.iter().any(|(_, p)| target.satisfied(&p)),
            None,
        ),
        timeout_duration(),
    );
    fut.await
}

pub async fn expect_host_state(
    host: &HostDriverHarness,
    target: Predicate<HostInfo>,
) -> Result<HostState, Error> {
    let fut = host.when_satisfied(
        Predicate::<HostState>::new(move |host| target.satisfied(&host.host_info), None),
        timeout_duration(),
    );
    fut.await
}

// Returns a future that resolves when a peer matching `id` is not present on the host.
pub async fn expect_no_peer(host: &HostDriverHarness, id: PeerId) -> Result<(), Error> {
    let fut = host.when_satisfied(
        Predicate::<HostState>::new(move |host| host.peers.iter().all(|(i, _)| i != &id), None),
        timeout_duration(),
    );
    fut.await?;
    Ok(())
}

pub struct HostState {
    emulator_state: EmulatorState,

    // Access to the bt-host device under test.
    host_path: PathBuf,

    // Current bt-host driver state.
    host_info: HostInfo,

    // All known remote devices, indexed by their identifiers.
    peers: HashMap<PeerId, Peer>,
}

impl HostState {
    pub fn peers(&self) -> &HashMap<PeerId, Peer> {
        &self.peers
    }
}

impl Clone for HostState {
    fn clone(&self) -> HostState {
        HostState {
            emulator_state: self.emulator_state.clone(),
            host_path: self.host_path.clone(),
            host_info: self.host_info.clone(),
            peers: self.peers.clone(),
        }
    }
}

impl AsMut<EmulatorState> for HostState {
    fn as_mut(&mut self) -> &mut EmulatorState {
        &mut self.emulator_state
    }
}

impl AsRef<EmulatorState> for HostState {
    fn as_ref(&self) -> &EmulatorState {
        &self.emulator_state
    }
}

pub type HostDriverHarness = ExpectationHarness<HostState, HostDriverHarnessAux>;
type HostDriverHarnessAux = EmulatorHarnessAux<HostProxy>;

impl TestHarness for HostDriverHarness {
    type Env = (PathBuf, Emulator);
    type Runner = BoxFuture<'static, Result<(), Error>>;

    fn init() -> BoxFuture<'static, Result<(Self, Self::Env, Self::Runner), Error>> {
        async {
            let (harness, emulator) = new_host_harness().await?;
            let watch_info = watch_host_info(harness.clone())
                .map_err(|e| e.context("Error watching host state"))
                .err_into();
            let watch_peers = watch_peers(harness.clone())
                .map_err(|e| e.context("Error watching peers"))
                .err_into();
            let watch_emulator_params = watch_controller_parameters(harness.clone())
                .map_err(|e| e.context("Error watching controller parameters"))
                .err_into();
            let path = harness.read().host_path;

            let run = future::try_join3(watch_info, watch_peers, watch_emulator_params)
                .map_ok(|((), (), ())| ())
                .boxed();
            Ok((harness, (path, emulator), run))
        }
        .boxed()
    }

    fn terminate(env: Self::Env) -> BoxFuture<'static, Result<(), Error>> {
        let (path, mut emulator) = env;
        async move {
            // Shut down the fake bt-hci device and make sure the bt-host device gets removed.
            let mut watcher = DeviceWatcher::new(HOST_DEVICE_DIR, timeout_duration()).await?;
            emulator.destroy_and_wait().await?;
            watcher.watch_removed(&path).await
        }
        .boxed()
    }
}

impl EmulatorHarness for HostDriverHarness {
    type State = HostState;

    fn emulator(&self) -> HciEmulatorProxy {
        self.aux().emulator().clone()
    }

    fn state(&self) -> MappedRwLockWriteGuard<'_, HostState> {
        self.write_state()
    }
}

// Creates a fake bt-hci device and returns the corresponding bt-host device once it gets created.
async fn new_host_harness() -> Result<(HostDriverHarness, Emulator), Error> {
    let emulator = Emulator::create("bt-integration-test-host").await?;
    let host_dev = emulator.publish_and_wait_for_host(Emulator::default_settings()).await?;

    // Open a Host FIDL interface channel to the bt-host device.
    let fidl_handle = host::open_host_channel(&host_dev.file())?;
    let host_proxy = HostProxy::new(fasync::Channel::from_channel(fidl_handle.into())?);

    let host_info = host_proxy.watch_state().await?.try_into()?;
    let host_path = host_dev.path().to_path_buf();
    let peers = HashMap::new();

    let harness = ExpectationHarness::init(
        HostDriverHarnessAux::new(host_proxy, emulator.emulator().clone()),
        HostState { emulator_state: EmulatorState::default(), host_path, host_info, peers },
    );
    Ok((harness, emulator))
}

async fn watch_peers(harness: HostDriverHarness) -> Result<(), Error> {
    loop {
        // Clone the proxy so that the aux() lock is not held while waiting.
        let proxy = harness.aux().proxy().clone();
        let (updated, removed) = proxy.watch_peers().await?;
        for peer in updated.into_iter() {
            let peer: Peer = peer.try_into()?;
            harness.write_state().peers.insert(peer.id.clone(), peer);
            harness.notify_state_changed();
        }
        for id in removed.into_iter() {
            harness.write_state().peers.remove(&id.into());
        }
    }
}

async fn watch_host_info(harness: HostDriverHarness) -> Result<(), Error> {
    loop {
        let proxy = harness.aux().proxy().clone();
        let info = proxy.watch_state().await?;
        harness.write_state().host_info = info.try_into()?;
        harness.notify_state_changed();
    }
}
