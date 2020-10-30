// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        constants::HOST_DEVICE_DIR,
        device_watcher::DeviceWatcher,
        expectation::{
            asynchronous::{
                expectable, Expectable, ExpectableExt, ExpectableState, ExpectableStateExt,
            },
            Predicate,
        },
        hci_emulator::Emulator,
        host,
        types::{HostInfo, Peer, PeerId},
    },
    futures::{
        future::{self, BoxFuture},
        FutureExt, TryFutureExt,
    },
    std::{
        collections::HashMap,
        convert::{AsMut, AsRef, TryInto},
        ops::{Deref, DerefMut},
        path::PathBuf,
    },
    test_harness::TestHarness,
};

use crate::{
    emulator::{watch_controller_parameters, EmulatorState},
    timeout_duration,
};

#[derive(Clone, Debug)]
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
    pub fn info(&self) -> &HostInfo {
        &self.host_info
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

/// Auxilliary data for the HostDriverHarness
pub struct Aux {
    pub host: HostProxy,
    pub emulator: HciEmulatorProxy,
}

impl AsRef<HciEmulatorProxy> for Aux {
    fn as_ref(&self) -> &HciEmulatorProxy {
        &self.emulator
    }
}

#[derive(Clone)]
pub struct HostDriverHarness(Expectable<HostState, Aux>);

impl Deref for HostDriverHarness {
    type Target = Expectable<HostState, Aux>;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for HostDriverHarness {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

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
            let watch_emulator_params = watch_controller_parameters(harness.0.clone())
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

// Creates a fake bt-hci device and returns the corresponding bt-host device once it gets created.
async fn new_host_harness() -> Result<(HostDriverHarness, Emulator), Error> {
    let emulator = Emulator::create("bt-integration-test-host")
        .await
        .context("Error creating emulator root device")?;
    let host_dev = emulator
        .publish_and_wait_for_host(Emulator::default_settings())
        .await
        .context("Error publishing emulator hci device")?;

    // Open a Host FIDL interface channel to the bt-host device.
    let fidl_handle =
        host::open_host_channel(&host_dev.file()).context("Error opening host device file")?;
    let host = HostProxy::new(
        fasync::Channel::from_channel(fidl_handle.into())
            .context("Error creating async channel from host device")?,
    );

    let host_info = host
        .watch_state()
        .await
        .context("Error calling WatchState()")?
        .try_into()
        .context("Invalid HostInfo received")?;
    let host_path = host_dev.path().to_path_buf();
    let peers = HashMap::new();

    let harness = HostDriverHarness(expectable(
        HostState { emulator_state: EmulatorState::default(), host_path, host_info, peers },
        Aux { host, emulator: emulator.emulator().clone() },
    ));

    Ok((harness, emulator))
}

async fn watch_peers(harness: HostDriverHarness) -> Result<(), Error> {
    loop {
        // Clone the proxy so that the aux() lock is not held while waiting.
        let proxy = harness.aux().host.clone();
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
        let proxy = harness.aux().host.clone();
        let info = proxy.watch_state().await?;
        harness.write_state().host_info = info.try_into()?;
        harness.notify_state_changed();
    }
}

pub mod expectation {
    use super::*;

    /// Returns a Future that resolves when the state of any RemoteDevice matches `target`.
    pub async fn peer(host: &HostDriverHarness, p: Predicate<Peer>) -> Result<HostState, Error> {
        host.when_satisfied(
            Predicate::any(p).over_value(
                |host: &HostState| host.peers.values().cloned().collect::<Vec<_>>(),
                ".peers.values()",
            ),
            timeout_duration(),
        )
        .await
    }

    /// Returns a Future that resolves when the HostInfo matches `target`.
    pub async fn host_state(
        host: &HostDriverHarness,
        p: Predicate<HostInfo>,
    ) -> Result<HostState, Error> {
        host.when_satisfied(
            p.over(|host: &HostState| &host.host_info, ".host_info"),
            timeout_duration(),
        )
        .await
    }

    /// Returns a Future that resolves when a peer matching `id` is not present on the host.
    pub async fn no_peer(host: &HostDriverHarness, id: PeerId) -> Result<(), Error> {
        let fut = host.when_satisfied(
            Predicate::all(Predicate::not_equal(id)).over_value(
                |host: &HostState| host.peers.keys().cloned().collect::<Vec<_>>(),
                ".peers.keys()",
            ),
            timeout_duration(),
        );
        fut.await?;
        Ok(())
    }
}
