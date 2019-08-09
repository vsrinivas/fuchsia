// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, ResultExt},
    fidl_fuchsia_bluetooth_control::{AdapterInfo, AdapterState, RemoteDevice},
    fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy},
    fuchsia_async as fasync,
    fuchsia_bluetooth::{
        constants::HOST_DEVICE_DIR,
        device_watcher::DeviceWatcher,
        error::Error as BtError,
        expectation::{
            asynchronous::{ExpectableState, ExpectableStateExt, ExpectationHarness},
            Predicate,
        },
        hci_emulator::Emulator,
        host,
        util::{clone_host_info, clone_host_state, clone_remote_device},
    },
    fuchsia_zircon::{Duration, DurationNum},
    futures::{Future, TryFutureExt, TryStreamExt},
    std::{borrow::Borrow, collections::HashMap, path::PathBuf},
};

use crate::harness::TestHarness;

const TIMEOUT_SECONDS: i64 = 10; // in seconds

pub fn timeout_duration() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

// Applies `delta` to `base`.
fn apply_delta(base: AdapterState, delta: AdapterState) -> AdapterState {
    AdapterState {
        local_name: delta.local_name.or(base.local_name),
        discoverable: delta.discoverable.or(base.discoverable),
        discovering: delta.discovering.or(base.discovering),
        local_service_uuids: delta.local_service_uuids.or(base.local_service_uuids),
    }
}

pub fn expect_remote_device(
    test_state: &HostDriverHarness,
    address: &str,
    expected: &Predicate<RemoteDevice>,
) -> Result<(), Error> {
    let state = test_state.read();
    let peer = state
        .peers
        .values()
        .find(|dev| dev.address == address)
        .ok_or(BtError::new(&format!("Peer with address '{}' not found", address)))?;
    expect_true!(expected.satisfied(peer))
}

// Returns a future that resolves when a peer matching `id` is not present on the host.
pub async fn expect_no_peer(host: &HostDriverHarness, id: String) -> Result<(), Error> {
    host.when_satisfied(
        Predicate::<HostState>::new(move |host| host.peers.iter().all(|(i, _)| i != &id), None),
        timeout_duration(),
    )
    .await?;
    Ok(())
}

pub type HostDriverHarness = ExpectationHarness<HostState, (HostProxy, Option<Emulator>)>;

pub struct HostState {
    // Access to the bt-host device under test.
    host_path: PathBuf,

    // Current bt-host driver state.
    host_info: AdapterInfo,

    // All known remote devices, indexed by their identifiers.
    peers: HashMap<String, RemoteDevice>,
}

impl Clone for HostState {
    fn clone(&self) -> HostState {
        HostState {
            host_path: self.host_path.clone(),
            host_info: clone_host_info(&self.host_info),
            peers: self.peers.iter().map(|(k, v)| (k.clone(), clone_remote_device(v))).collect(),
        }
    }
}

// Creates a fake bt-hci device and returns the corresponding bt-host device once it gets created.
async fn new_host_harness() -> Result<HostDriverHarness, Error> {
    let emulator = Emulator::create("bt-integration-test-host").await?;
    let host_dev = emulator.publish_and_wait_for_host(Emulator::default_settings()).await?;

    // Open a Host FIDL interface channel to the bt-host device.
    let fidl_handle = host::open_host_channel(&host_dev.file())?;
    let host_proxy = HostProxy::new(fasync::Channel::from_channel(fidl_handle.into())?);

    let host_info = host_proxy.get_info().await?;
    let host_path = host_dev.path().to_path_buf();
    let peers = HashMap::new();

    Ok(ExpectationHarness::init(
        (host_proxy, Some(emulator)),
        HostState { host_path, host_info, peers },
    ))
}

// Returns a Future that resolves when the state of any RemoteDevice matches `target`.
pub async fn expect_host_peer(
    host: &HostDriverHarness,
    target: Predicate<RemoteDevice>,
) -> Result<HostState, Error> {
    host.when_satisfied(
        Predicate::<HostState>::new(
            move |host| host.peers.iter().any(|(_, p)| target.satisfied(p)),
            None,
        ),
        timeout_duration(),
    )
    .await
}

pub async fn expect_adapter_state(
    host: &HostDriverHarness,
    target: Predicate<AdapterState>,
) -> Result<HostState, Error> {
    host.when_satisfied(
        Predicate::<HostState>::new(
            move |host| match &host.host_info.state {
                Some(state) => target.satisfied(state),
                None => false,
            },
            None,
        ),
        timeout_duration(),
    )
    .await
}

impl TestHarness for HostDriverHarness {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(run_host_harness_test_async(test_func))
    }
}

// Returns a Future that handles Host interface events.
async fn handle_host_events(harness: HostDriverHarness) -> Result<(), Error> {
    let mut events = harness.aux().0.take_event_stream();

    while let Some(e) = events.try_next().await? {
        match e {
            HostEvent::OnAdapterStateChanged { state } => {
                let host_info = &mut harness.write_state().host_info;
                let base = match host_info.state {
                    Some(ref state) => clone_host_state(state.borrow()),
                    None => AdapterState {
                        local_name: None,
                        discoverable: None,
                        discovering: None,
                        local_service_uuids: None,
                    },
                };
                let new_state = apply_delta(base, state);
                host_info.state = Some(Box::new(new_state));
            }
            HostEvent::OnDeviceUpdated { device } => {
                harness.write_state().peers.insert(device.identifier.clone(), device);
            }
            HostEvent::OnDeviceRemoved { identifier } => {
                harness.write_state().peers.remove(&identifier);
            }
            // TODO(armansito): handle other events
            e => {
                eprintln!("Unhandled event: {:?}", e);
            }
        }
        harness.notify_state_changed();
    }

    Ok(())
}

async fn run_host_harness_test_async<F, Fut>(test_func: F) -> Result<(), Error>
where
    F: FnOnce(HostDriverHarness) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let host_test = new_host_harness().await?;

    // Start processing events in a background task.
    fasync::spawn(
        handle_host_events(host_test.clone())
            .unwrap_or_else(|e| eprintln!("Error handling host events: {:?}", e)),
    );

    // Run the test and obtain the test result.
    let result = test_func(host_test.clone()).await;

    // Shut down the fake bt-hci device and make sure the bt-host device gets removed.
    let mut watcher = DeviceWatcher::new(HOST_DEVICE_DIR, timeout_duration()).await?;
    let host_path = &host_test.read().host_path;
    host_test.aux().1 = None;
    watcher.watch_removed(host_path).await?;
    result
}
