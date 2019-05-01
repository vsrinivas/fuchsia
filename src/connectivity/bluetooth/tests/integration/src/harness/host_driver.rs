// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_control::{AdapterInfo, AdapterState, RemoteDevice},
    fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy},
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_bluetooth::{
        error::Error as BtError, expectation::Predicate, fake_hci::FakeHciDevice, hci, host,
        util::clone_host_state,
    },
    fuchsia_vfs_watcher::{WatchEvent as VfsWatchEvent, Watcher as VfsWatcher},
    fuchsia_zircon::{Duration, DurationNum},
    futures::{future, task, Future, FutureExt, Poll, TryFutureExt, TryStreamExt},
    parking_lot::{MappedRwLockReadGuard, RwLock, RwLockReadGuard},
    slab::Slab,
    std::{borrow::Borrow, collections::HashMap, fs::File, path::PathBuf, pin::Pin, sync::Arc},
};

use crate::harness::TestHarness;

const BT_HOST_DIR: &str = "/dev/class/bt-host";
const TIMEOUT_SECONDS: i64 = 10; // in seconds

fn timeout_duration() -> Duration {
    TIMEOUT_SECONDS.seconds()
}

#[derive(Clone)]
pub struct HostDriverHarness(Arc<RwLock<HostDriverHarnessInner>>);

struct HostDriverHarnessInner {
    // Fake bt-hci device.
    fake_hci_dev: Option<FakeHciDevice>,

    // Access to the bt-host device under test.
    host_path: String,
    pub host_proxy: HostProxy,

    // Current bt-host driver state.
    host_info: AdapterInfo,

    // All known remote devices, indexed by their identifiers.
    remote_devices: HashMap<String, RemoteDevice>,

    // Tasks that are interested in being woken up when the host drirver state changes.
    host_state_tasks: Slab<task::Waker>,
}

impl HostDriverHarness {
    // Returns a Future that resolves when the bt-host transitions to a state that matches the
    // predicate `target`.
    //
    // For example, if `target` is
    //
    //   expectation::host_driver::discoverable(true)
    //     .and(expectation::host_driver::discovering(true));
    //
    // then the Future will resolve when the host driver becomes discoverable AND discovering. Other
    // fields will be ignored.
    //
    // If the host driver is already in the requested state, then the Future will resolve the first
    // time it gets polled.
    pub async fn expect(&self, target: Predicate<AdapterState>) -> Result<AdapterState, Error> {
        let err_msg = format!("timed out waiting for host driver state (expected: {:?})", target);
        await!(HostDriverStateFuture::new(self.clone(), target)
            .on_timeout(timeout_duration().after_now(), move || Err(BtError::new(&err_msg).into())))
    }

    // Returns a Future that resolves when the state of a particular RemoteDevice matches
    // `target`. If `id` is None, then the Future will resolve when any device matches
    // `target`. Otherwise, the Future will resolve when the state of the requested device
    // changes.
    pub async fn expect_peer(
        &self,
        id: Option<String>,
        target: Predicate<RemoteDevice>,
    ) -> Result<(), Error> {
        let err_msg = format!("timed out waiting for remote device state (expected: {:?})", target);
        await!(RemoteDeviceStateFuture::new(self.clone(), id, Some(target))
            .on_timeout(timeout_duration().after_now(), move || Err(BtError::new(&err_msg).into())))
    }

    pub fn host_proxy(&self) -> MappedRwLockReadGuard<HostProxy> {
        RwLockReadGuard::map(self.0.read(), |host| &host.host_proxy)
    }
}

impl HostDriverHarnessInner {
    fn new(
        hci: FakeHciDevice,
        host_path: String,
        host: HostProxy,
        info: AdapterInfo,
    ) -> HostDriverHarness {
        HostDriverHarness(Arc::new(RwLock::new(HostDriverHarnessInner {
            fake_hci_dev: Some(hci),
            host_path: host_path,
            host_proxy: host,
            host_info: info,
            remote_devices: HashMap::new(),
            host_state_tasks: Slab::new(),
        })))
    }

    // Returns a Future that handles Host interface events.
    fn events_future(test_state: HostDriverHarness) -> impl Future<Output = Result<(), Error>> {
        let stream = test_state.0.read().host_proxy.take_event_stream();
        stream
            .try_for_each(move |evt| {
                match evt {
                    HostEvent::OnAdapterStateChanged { state } => {
                        test_state.0.write().handle_driver_state_changed(state);
                    }
                    HostEvent::OnDeviceUpdated { device } => {
                        test_state.0.write().handle_device_updated(device);
                    }
                    HostEvent::OnDeviceRemoved { identifier } => {
                        test_state.0.write().handle_device_removed(identifier);
                    }
                    // TODO(armansito): handle other events
                    evt => {
                        eprintln!("Unhandled event: {:?}", evt);
                    }
                }
                future::ready(Ok(()))
            })
            .err_into()
    }

    fn close_fake_hci(&mut self) {
        self.fake_hci_dev = None;
    }

    fn store_task(&mut self, task: task::Waker) -> usize {
        self.host_state_tasks.insert(task)
    }

    fn remove_task(&mut self, key: usize) {
        if self.host_state_tasks.contains(key) {
            self.host_state_tasks.remove(key);
        }
    }

    fn find_device_by_address(&self, address: &str) -> Result<&RemoteDevice, Error> {
        self.remote_devices
            .values()
            .find(|dev| dev.address == address)
            .ok_or(BtError::new(&format!("device with address '{}' not found", address)).into())
    }

    // Handle the OnAdapterStateChanged event.
    fn handle_driver_state_changed(&mut self, state_change: AdapterState) {
        let base = match self.host_info.state {
            Some(ref state) => clone_host_state(state.borrow()),
            None => AdapterState {
                local_name: None,
                discoverable: None,
                discovering: None,
                local_service_uuids: None,
            },
        };
        let new_state = apply_delta(base, state_change);
        self.host_info.state = Some(Box::new(new_state));
        self.notify_host_state_changed();
    }

    // Handle the OnDeviceUpdated event
    fn handle_device_updated(&mut self, device: RemoteDevice) {
        self.remote_devices.insert(device.identifier.clone(), device);
        self.notify_host_state_changed();
    }

    // Handle the OnDeviceRemoved event
    fn handle_device_removed(&mut self, id: String) {
        self.remote_devices.remove(&id);
        self.notify_host_state_changed();
    }

    fn notify_host_state_changed(&mut self) {
        for task in &self.host_state_tasks {
            task.1.wake_by_ref();
        }
        self.host_state_tasks.clear()
    }
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

// A future that resolves when the state of a remote device changes to a target value.
struct RemoteDeviceStateFuture {
    inner: StateUpdateFutureInner,

    // The identifier of the desired RemoteDevice. If None, then this Future can resolve with any
    // device.
    target_dev_id: Option<String>,

    // The expected state a RemoteDevice is expected to reach. If the state is
    // None then the Future will resolve when the device gets removed. If both `target_dev_state`
    // and `target_dev_id` are None, then the Future will resolve when any device gets removed.
    target_dev_state: Option<Predicate<RemoteDevice>>,
}

impl RemoteDeviceStateFuture {
    fn new(
        test: HostDriverHarness,
        target_id: Option<String>,
        target_state: Option<Predicate<RemoteDevice>>,
    ) -> Self {
        RemoteDeviceStateFuture {
            inner: StateUpdateFutureInner::new(test),
            target_dev_id: target_id,
            target_dev_state: target_state,
        }
    }

    fn look_for_match(&self) -> bool {
        match &self.target_dev_id {
            None => self.match_any_device(&self.target_dev_state),
            Some(id) => self.match_device_by_id(&id, &self.target_dev_state),
        }
    }

    fn match_any_device(&self, target: &Option<Predicate<RemoteDevice>>) -> bool {
        target.as_ref().map_or(false, |target| {
            self.inner
                .state
                .0
                .read()
                .remote_devices
                .values()
                .find(|dev| target.satisfied(dev))
                .is_some()
        })
    }

    fn match_device_by_id(&self, id: &str, target: &Option<Predicate<RemoteDevice>>) -> bool {
        match (self.inner.state.0.read().remote_devices.get(id), target) {
            (None, None) => true,
            (Some(dev), Some(target)) => target.satisfied(dev),
            _ => false,
        }
    }
}
struct StateUpdateFutureInner {
    state: HostDriverHarness,
    waker_key: Option<usize>,
}

impl StateUpdateFutureInner {
    fn new(state: HostDriverHarness) -> StateUpdateFutureInner {
        StateUpdateFutureInner { state, waker_key: None }
    }

    fn clear_waker(&mut self) {
        if let Some(key) = self.waker_key {
            self.state.0.write().remove_task(key);
            self.waker_key = None;
        }
    }

    fn store_task(&mut self, w: &task::Waker) {
        let key = self.state.0.write().store_task(w.clone());
        self.waker_key = Some(key);
    }
}

// A future that resolves when the bt-host's AdapterState changes to a certain target value.
struct HostDriverStateFuture {
    inner: StateUpdateFutureInner,

    // The expected host driver state.
    target_host_state: Predicate<AdapterState>,
}

impl HostDriverStateFuture {
    fn new(test: HostDriverHarness, target_state: Predicate<AdapterState>) -> Self {
        HostDriverStateFuture {
            inner: StateUpdateFutureInner::new(test),
            target_host_state: target_state,
        }
    }
}

impl std::marker::Unpin for HostDriverStateFuture {}

#[must_use = "futures do nothing unless polled"]
impl Future for HostDriverStateFuture {
    type Output = Result<AdapterState, Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> Poll<Self::Output> {
        self.inner.clear_waker();
        if let Some(state) = &self.inner.state.0.read().host_info.state {
            if self.target_host_state.satisfied(state.borrow()) {
                return Poll::Ready(Ok(clone_host_state(&state.borrow())));
            }
        };
        self.inner.store_task(cx.waker());
        Poll::Pending
    }
}

impl std::marker::Unpin for RemoteDeviceStateFuture {}

#[must_use = "futures do nothing unless polled"]
impl Future for RemoteDeviceStateFuture {
    type Output = Result<(), Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut task::Context<'_>) -> Poll<Self::Output> {
        self.inner.clear_waker();
        if self.look_for_match() {
            Poll::Ready(Ok(()))
        } else {
            self.inner.store_task(cx.waker());
            Poll::Pending
        }
    }
}

// Returns a Future that resolves when a bt-host device gets added under the given topological
// path.
async fn watch_for_new_host_helper(
    mut watcher: VfsWatcher,
    parent_topo_path: String,
) -> Result<(File, PathBuf), Error> {
    while let Some(msg) = await!(watcher.try_next())? {
        match msg.event {
            VfsWatchEvent::EXISTING | VfsWatchEvent::ADD_FILE => {
                let path =
                    PathBuf::from(format!("{}/{}", BT_HOST_DIR, msg.filename.to_string_lossy()));
                let host_fd = hci::open_rdwr(&path)?;
                let host_topo_path = fdio::device_get_topo_path(&host_fd)?;
                if host_topo_path.starts_with(parent_topo_path.as_str()) {
                    return Ok((host_fd, path.clone()));
                }
            }
            _ => (),
        }
    }
    unreachable!();
}

// Returns a Future that resolves when the bt-host device with the given path gets removed.
async fn wait_for_host_removal_helper(mut watcher: VfsWatcher, path: String) -> Result<(), Error> {
    while let Some(msg) = await!(watcher.try_next())? {
        match msg.event {
            VfsWatchEvent::REMOVE_FILE => {
                let actual_path = format!("{}/{}", BT_HOST_DIR, msg.filename.to_string_lossy());
                if path == actual_path {
                    return Ok(());
                }
            }
            _ => (),
        }
    }
    unreachable!();
}

// Wraps a Future inside a timeout that logs a message and resolves to an error on expiration.
fn timeout<T, F>(fut: F, msg: &'static str) -> impl Future<Output = Result<T, Error>>
where
    F: Future<Output = Result<T, Error>>,
{
    fut.on_timeout(timeout_duration().after_now(), move || Err(BtError::new(msg).into()))
}

async fn watch_for_host(watcher: VfsWatcher, hci_path: String) -> Result<(File, PathBuf), Error> {
    await!(timeout(watch_for_new_host_helper(watcher, hci_path), "timed out waiting for bt-host"))
}

async fn wait_for_host_removal(watcher: VfsWatcher, path: String) -> Result<(), Error> {
    await!(timeout(
        wait_for_host_removal_helper(watcher, path),
        "timed out waiting for bt-host removal"
    ))
}

// Creates a fake bt-hci device and returns the corresponding bt-host device once it gets created.
async fn setup_emulated_host_test() -> Result<HostDriverHarness, Error> {
    let fake_hci = FakeHciDevice::new("bt-hci-integration-test-0")?;
    let fake_hci_topo_path = fdio::device_get_topo_path(fake_hci.file())?;

    let dir = File::open(&BT_HOST_DIR)?;
    let watcher = VfsWatcher::new(&dir)?;
    let (host_dev_fd, path) = await!(watch_for_host(watcher, fake_hci_topo_path))?;

    // Open a Host FIDL interface channel to the bt-host device.
    let fidl_handle = host::open_host_channel(&host_dev_fd)?;
    let host = HostProxy::new(fasync::Channel::from_channel(fidl_handle.into())?);
    let info = await!(host.get_info())?;

    Ok(HostDriverHarnessInner::new(fake_hci, path.to_string_lossy().to_string(), host, info))
}

async fn run_host_test_async<F, Fut>(test_func: F) -> Result<(), Error>
where
    F: FnOnce(HostDriverHarness) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let host_test = await!(setup_emulated_host_test())?;

    // Start processing events in a background task.
    fasync::spawn(HostDriverHarnessInner::events_future(host_test.clone()).map(|_| ()));

    // Run the test and obtain the test result.
    let result = await!(test_func(host_test.clone()));

    // Shut down the fake bt-hci device and make sure the bt-host device gets removed.
    let dir = File::open(&BT_HOST_DIR)?;
    let watcher = VfsWatcher::new(&dir)?;
    host_test.0.write().close_fake_hci();
    await!(wait_for_host_removal(watcher, host_test.0.read().host_path.clone()))?;
    result
}

impl TestHarness for HostDriverHarness {
    fn run_with_harness<F, Fut>(test_func: F) -> Result<(), Error>
    where
        F: FnOnce(Self) -> Fut,
        Fut: Future<Output = Result<(), Error>>,
    {
        let mut executor = fasync::Executor::new().context("error creating event loop")?;
        executor.run_singlethreaded(run_host_test_async(test_func))
    }
}

pub fn expect_eq<T>(expected: &T, actual: &T) -> Result<(), Error>
where
    T: std::fmt::Debug + std::cmp::PartialEq,
{
    if *expected == *actual {
        Ok(())
    } else {
        Err(BtError::new(&format!("failed - expected '{:#?}', found: '{:#?}'", expected, actual))
            .into())
    }
}

macro_rules! expect_eq {
    ($expected:expr, $actual:expr) => {
        expect_eq(&$expected, &$actual)
    };
}

macro_rules! expect_true {
    ($condition:expr) => {
        if $condition{
            Ok(())
        } else {
            Err(fuchsia_bluetooth::error::Error::new(&format!(
                "condition is not true: {}",
                stringify!($condition)
            )).into())
        } as Result<(), Error>
    }
}

pub fn expect_remote_device(
    test_state: &HostDriverHarness,
    address: &str,
    expected: &Predicate<RemoteDevice>,
) -> Result<(), Error> {
    expect_true!(expected.satisfied(test_state.0.read().find_device_by_address(address)?))
}
