// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth::{Bool, Status},
    fidl_fuchsia_bluetooth_control::{AdapterInfo, AdapterState, RemoteDevice, TechnologyType},
    fidl_fuchsia_bluetooth_host::{
        AddressType, BondingData, HostEvent, HostProxy, Key, LeData, Ltk, SecurityProperties,
    },
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_bluetooth::{
        error::Error as BtError, fake_hci::FakeHciDevice, host, util::clone_host_state,
    },
    fuchsia_vfs_watcher::{WatchEvent as VfsWatchEvent, Watcher as VfsWatcher},
    fuchsia_zircon::DurationNum,
    futures::{future, task, Future, FutureExt, Poll, TryFutureExt, TryStreamExt},
    parking_lot::RwLock,
    slab::Slab,
    std::{
        borrow::Borrow,
        collections::HashMap,
        fs::File,
        io::{self, Write},
        path::PathBuf,
        pin::Pin,
        sync::Arc,
    },
};

mod common;

const TIMEOUT: i64 = 10; // in seconds
const BT_HOST_DIR: &str = "/dev/class/bt-host";

type HostTestPtr = Arc<RwLock<HostTest>>;

struct HostTest {
    // Fake bt-hci device.
    fake_hci_dev: Option<FakeHciDevice>,

    // Access to the bt-host device under test.
    host_path: String,
    host_proxy: HostProxy,

    // Current bt-host adapter state.
    host_info: AdapterInfo,

    // All known remote devices, indexed by their identifiers.
    remote_devices: HashMap<String, RemoteDevice>,

    // Tasks that are interested in being woken up when the adapter state changes.
    host_state_tasks: Slab<task::Waker>,
}

impl HostTest {
    fn new(
        hci: FakeHciDevice,
        host_path: String,
        host: HostProxy,
        info: AdapterInfo,
    ) -> HostTestPtr {
        Arc::new(RwLock::new(HostTest {
            fake_hci_dev: Some(hci),
            host_path: host_path,
            host_proxy: host,
            host_info: info,
            remote_devices: HashMap::new(),
            host_state_tasks: Slab::new(),
        }))
    }

    // Returns a Future that handles Host interface events.
    fn events_future(test_state: HostTestPtr) -> impl Future<Output = Result<(), Error>> {
        let stream = test_state.read().host_proxy.take_event_stream();
        stream
            .try_for_each(move |evt| {
                match evt {
                    HostEvent::OnAdapterStateChanged { state } => {
                        test_state.write().handle_adapter_state_changed(state);
                    }
                    HostEvent::OnDeviceUpdated { device } => {
                        test_state.write().handle_device_updated(device);
                    }
                    HostEvent::OnDeviceRemoved { identifier } => {
                        test_state.write().handle_device_removed(identifier);
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

    // Returns a Future that resolves when the bt-host transitions to a state that matches the
    // valid fields of `target`. For example, if `target_state` is
    //
    //    AdapterState {
    //        local_name: None,
    //        discoverable: Some(Box::new(Bool { value: true })),
    //        discovering: Some(Box::new(Bool { value: true })),
    //        local_service_uuids: None
    //    }
    //
    // then the Future will resolve when the adapter becomes discoverable AND discovering. Other
    // fields will be ignored.
    //
    // If the adapter is already in the requested state, then the Future will resolve the first
    // time it gets polled.
    fn on_adapter_state_change(
        test_state: HostTestPtr,
        target_state: AdapterState,
    ) -> impl Future<Output = Result<AdapterState, Error>> {
        let err_msg = format!("timed out waiting for adapter state (expected: {:?})", target_state);
        AdapterStateFuture::new(test_state, target_state)
            .on_timeout(TIMEOUT.seconds().after_now(), move || Err(BtError::new(&err_msg).into()))
    }

    // Returns a Future that resolves when the state of a particular RemoteDevice matches
    // `target_state`. If `id` is None, then the Future will resolve when any device matches
    // `target_state`. Otherwise, the Future will resolve when the state of the requested device
    // changes.
    fn on_device_update<'a>(
        test_state: HostTestPtr,
        id: Option<&'a str>,
        target_state: RemoteDeviceExpectation,
    ) -> impl Future<Output = Result<(), Error>> + 'a {
        let err_msg =
            format!("timed out waiting for remote device state (expected: {:?})", target_state);
        RemoteDeviceStateFuture::new(test_state, id, Some(target_state))
            .on_timeout(TIMEOUT.seconds().after_now(), move || Err(BtError::new(&err_msg).into()))
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
    fn handle_adapter_state_changed(&mut self, state_change: AdapterState) {
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
            task.1.wake();
        }
        self.host_state_tasks.clear()
    }
}

// Returns the value of `field` in `delta` if it's valid. Otherwise returns the value in `base`.
macro_rules! updated_field {
    ($base:ident, $delta:ident, $field:ident) => {
        match $delta.$field {
            None => $base.$field,
            value => value,
        }
    };
}

// Applies `delta` to `base`.
fn apply_delta(base: AdapterState, delta: AdapterState) -> AdapterState {
    AdapterState {
        local_name: updated_field!(base, delta, local_name),
        discoverable: updated_field!(base, delta, discoverable),
        discovering: updated_field!(base, delta, discovering),
        local_service_uuids: updated_field!(base, delta, local_service_uuids),
    }
}

// Macro for comparing optional fields within a target against fields in `base`. Fields of `target`
// are assumed to have the Option type while the optionality of a `base` field is determined by
// the syntax. For example, given:
//
//   struct A {
//     foo: i64,
//     bar: Option<String>,
//   }
//
//   struct Target {
//     foo: Option<i64>,
//     bar: Option<String>,
//   }
//
// Fields can be matched by invoking: compare_fields!(a, b, [foo, (bar)])
macro_rules! compare_field {
    ($base:ident, $target:ident, &$field:ident) => {
        compare_field!($base, $target, &$field)
    };

    // Compare fields assuming the base field is not an Option type.
    ($base:ident, $target:ident, $field:ident) => {
        match &$target.$field {
            None => None,
            Some(value) => Some(value == &$base.$field),
        }
    };

    // Compare fields assuming the base field is an Option type.
    ($base:ident, $target:ident, ($field:ident)) => {
        match &$target.$field {
            None => None,
            value => Some(value == &$base.$field),
        }
    };
}

macro_rules! compare_fields {
    ($base:ident, $target:ident, [$($field:tt),*]) => {{
        let mut all_none = true;
        $({
            if let Some(matched) = compare_field!($base, $target, $field) {
                all_none = false;
                if !matched {
                    return false;
                }
            }
        })*
        !all_none
    }};
}

// Struct for comparing individual fields a RemoteDevice.
#[derive(Clone, Debug, Default)]
struct RemoteDeviceExpectation {
    name: Option<String>,
    address: Option<String>,
    technology: Option<TechnologyType>,
    connected: Option<bool>,
    bonded: Option<bool>,
}

// Returns true if all valid fields of `target` match their equivalents in `base`. Returns false if
// there are any mismatches or if all fields of `target` are None.
fn compare_adapter_states(base: &AdapterState, target: &AdapterState) -> bool {
    compare_fields!(
        base,
        target,
        [(local_name), (discoverable), (discovering), (local_service_uuids)]
    )
}

fn compare_remote_device(base: &RemoteDevice, target: &RemoteDeviceExpectation) -> bool {
    compare_fields!(base, target, [(name), address, technology, connected, bonded])
}

struct StateUpdateFutureInner {
    test_state: HostTestPtr,
    waker_key: Option<usize>,
}

impl StateUpdateFutureInner {
    fn new(test: HostTestPtr) -> StateUpdateFutureInner {
        StateUpdateFutureInner { test_state: test, waker_key: None }
    }

    fn maybe_remove_waker(&mut self) {
        if let Some(key) = self.waker_key {
            self.test_state.write().remove_task(key);
            self.waker_key = None;
        }
    }

    fn store_task(&mut self, lw: &task::LocalWaker) {
        let key = self.test_state.write().store_task(lw.clone().into_waker());
        self.waker_key = Some(key);
    }
}

// A future that resolves when the bt-host's AdapterState changes to a certain target value.
struct AdapterStateFuture {
    inner: StateUpdateFutureInner,

    // The expected adapter state.
    target_host_state: AdapterState,
}

impl AdapterStateFuture {
    fn new(test: HostTestPtr, target_state: AdapterState) -> Self {
        AdapterStateFuture {
            inner: StateUpdateFutureInner::new(test),
            target_host_state: target_state,
        }
    }
}

impl std::marker::Unpin for AdapterStateFuture {}

#[must_use = "futures do nothing unless polled"]
impl Future for AdapterStateFuture {
    type Output = Result<AdapterState, Error>;

    fn poll(mut self: Pin<&mut Self>, lw: &task::LocalWaker) -> Poll<Self::Output> {
        self.inner.maybe_remove_waker();
        let states_match: bool = match &self.inner.test_state.read().host_info.state {
            None => false,
            Some(state) => compare_adapter_states(state.borrow(), &self.target_host_state),
        };
        if states_match {
            Poll::Ready(Ok(clone_host_state(&self.target_host_state)))
        } else {
            self.inner.store_task(lw);
            Poll::Pending
        }
    }
}

// A future that resolves when the state of a remote device changes to a target value.
struct RemoteDeviceStateFuture<'a> {
    inner: StateUpdateFutureInner,

    // The identifier of the desired RemoteDevice. If None, then this Future can resolve with any
    // device.
    target_dev_id: Option<&'a str>,

    // The expected state a RemoteDevice is expected to reach. If the state is
    // None then the Future will resolve when the device gets removed. If both `target_dev_state`
    // and `target_dev_id` are None, then the Future will resolve when any device gets removed.
    target_dev_state: Option<RemoteDeviceExpectation>,
}

impl<'a> RemoteDeviceStateFuture<'a> {
    fn new(
        test: HostTestPtr,
        target_id: Option<&'a str>,
        target_state: Option<RemoteDeviceExpectation>,
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
            Some(id) => self.match_device_by_id(id, &self.target_dev_state),
        }
    }

    fn match_any_device(&self, target: &Option<RemoteDeviceExpectation>) -> bool {
        target.as_ref().map_or(false, |target| {
            self.inner
                .test_state
                .read()
                .remote_devices
                .values()
                .find(|dev| compare_remote_device(dev, &target))
                .is_some()
        })
    }

    fn match_device_by_id(&self, id: &'a str, target: &Option<RemoteDeviceExpectation>) -> bool {
        match self.inner.test_state.read().remote_devices.get(id) {
            None => target.is_none(),
            Some(dev) => match target {
                None => false,
                Some(target) => compare_remote_device(dev, target),
            },
        }
    }
}

impl<'a> std::marker::Unpin for RemoteDeviceStateFuture<'a> {}

#[must_use = "futures do nothing unless polled"]
impl<'a> Future for RemoteDeviceStateFuture<'a> {
    type Output = Result<(), Error>;

    fn poll(mut self: Pin<&mut Self>, lw: &task::LocalWaker) -> Poll<Self::Output> {
        self.inner.maybe_remove_waker();
        if self.look_for_match() {
            Poll::Ready(Ok(()))
        } else {
            self.inner.store_task(lw);
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
                let host_fd = common::open_rdwr(&path)?;
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
    fut.on_timeout(TIMEOUT.seconds().after_now(), move || Err(BtError::new(msg).into()))
}

fn watch_for_new_host(
    watcher: VfsWatcher,
    fake_hci_topo_path: String,
) -> impl Future<Output = Result<(File, PathBuf), Error>> {
    timeout(watch_for_new_host_helper(watcher, fake_hci_topo_path), "timed out waiting for bt-host")
}

fn wait_for_host_removal(
    watcher: VfsWatcher,
    path: String,
) -> impl Future<Output = Result<(), Error>> {
    timeout(wait_for_host_removal_helper(watcher, path), "timed out waiting for bt-host removal")
}

// Creates a fake bt-hci device and returns the corresponding bt-host device once it gets created.
async fn setup_emulated_host_test() -> Result<HostTestPtr, Error> {
    let fake_hci = FakeHciDevice::new()?;
    let fake_hci_topo_path = fdio::device_get_topo_path(fake_hci.file())?;

    let dir = File::open(&BT_HOST_DIR)?;
    let watcher = VfsWatcher::new(&dir)?;
    let (host_dev_fd, path) = await!(watch_for_new_host(watcher, fake_hci_topo_path))?;

    // Open a Host FIDL interface channel to the bt-host device.
    let fidl_handle = host::open_host_channel(&host_dev_fd)?;
    let host = HostProxy::new(fasync::Channel::from_channel(fidl_handle.into())?);
    let info = await!(host.get_info())?;

    Ok(HostTest::new(fake_hci, path.to_string_lossy().to_string(), host, info))
}

async fn run_test_async<F, Fut>(test_func: F) -> Result<(), Error>
where
    F: FnOnce(HostTestPtr) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let host_test = await!(setup_emulated_host_test())?;

    // Start processing events in a background task.
    fasync::spawn(HostTest::events_future(host_test.clone()).map(|_| ()));

    // Run the test and obtain the test result.
    let result = await!(test_func(host_test.clone()));

    // Shut down the fake bt-hci device and make sure the bt-host device gets removed.
    let dir = File::open(&BT_HOST_DIR)?;
    let watcher = VfsWatcher::new(&dir)?;
    host_test.write().close_fake_hci();
    await!(wait_for_host_removal(watcher, host_test.read().host_path.clone()))?;

    if result.is_ok() {
        println!("\x1b[32mPASSED\x1b[0m");
    } else {
        println!("\x1b[31mFAILED\x1b[0m");
    }
    result
}

fn run_test<F, Fut>(test_func: F) -> Result<(), Error>
where
    F: FnOnce(HostTestPtr) -> Fut,
    Fut: Future<Output = Result<(), Error>>,
{
    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    executor.run_singlethreaded(run_test_async(test_func))
}

// Prints out the test name and runs the test.
macro_rules! run_test {
    ($name:ident) => {{
        print!("{}...", stringify!($name));
        io::stdout().flush().unwrap();
        run_test($name)
    }};
}

fn expect_eq<T>(expected: &T, actual: &T) -> Result<(), Error>
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
            Err(BtError::new(&format!(
                "condition is not true: {}",
                stringify!($condition)
            )).into())
        } as Result<(), Error>
    }
}

fn expect_remote_device(
    test_state: &HostTestPtr,
    address: &str,
    expected: &RemoteDeviceExpectation,
) -> Result<(), Error> {
    expect_true!(compare_remote_device(
        test_state.read().find_device_by_address(address)?,
        expected
    ))
}

// ========= Test Cases =========

// Tests that the device address is 0.
async fn test_bd_addr(test_state: HostTestPtr) -> Result<(), Error> {
    let info = await!(test_state.read().host_proxy.get_info())
        .map_err(|_| BtError::new("failed to read adapter info"))?;
    expect_eq!("00:00:00:00:00:00", info.address.as_str())
}

// Tests that setting the local name succeeds.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_set_local_name(test_state: HostTestPtr) -> Result<(), Error> {
    let name = "test1234";
    await!(test_state.read().host_proxy.set_local_name(&name))?;
    let state_change = AdapterState {
        local_name: Some(name.to_string()),
        discoverable: None,
        discovering: None,
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), state_change))?;

    Ok(())
}

// Tests that host state updates when discoverable mode is turned on.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_discoverable(test_state: HostTestPtr) -> Result<(), Error> {
    // Enable discoverable mode.
    await!(test_state.read().host_proxy.set_discoverable(true))?;
    let discoverable_state = AdapterState {
        local_name: None,
        discoverable: Some(Box::new(Bool { value: true })),
        discovering: None,
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), discoverable_state))?;

    // Disable discoverable mode
    await!(test_state.read().host_proxy.set_discoverable(false))?;
    let non_discoverable_state = AdapterState {
        local_name: None,
        discoverable: Some(Box::new(Bool { value: false })),
        discovering: None,
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), non_discoverable_state))?;

    Ok(())
}

// Tests that host state updates when discovery is started and stopped.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_discovery(test_state: HostTestPtr) -> Result<(), Error> {
    // Start discovery. "discovering" should get set to true.
    await!(test_state.read().host_proxy.start_discovery())?;
    let discovering_state = AdapterState {
        local_name: None,
        discoverable: None,
        discovering: Some(Box::new(Bool { value: true })),
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), discovering_state))?;

    // The host should discover a fake device.
    // TODO(NET-1457): The name is currently hard-coded in
    // garnet/drivers/bluetooth/hci/fake/fake-device.cpp:89. Configure this dynamically when it is
    // supported.
    let new_device =
        RemoteDeviceExpectation { name: Some("Fake".to_string()), ..Default::default() };
    await!(HostTest::on_device_update(test_state.clone(), None, new_device))?;

    // Stop discovery. "discovering" should get set to false.
    await!(test_state.read().host_proxy.stop_discovery())?;
    let not_discovering_state = AdapterState {
        local_name: None,
        discoverable: None,
        discovering: Some(Box::new(Bool { value: false })),
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), not_discovering_state))?;

    Ok(())
}

// Tests that "close" cancels all operations.
// TODO(armansito): Test for FakeHciDevice state changes.
async fn test_close(test_state: HostTestPtr) -> Result<(), Error> {
    // Enable all procedures.
    await!(test_state.read().host_proxy.start_discovery())?;
    await!(test_state.read().host_proxy.set_discoverable(true))?;

    let active_state = AdapterState {
        local_name: None,
        discoverable: Some(Box::new(Bool { value: true })),
        discovering: Some(Box::new(Bool { value: true })),
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), active_state))?;

    // Close should cancel these procedures.
    test_state.read().host_proxy.close()?;
    let closed_state_update = AdapterState {
        local_name: None,
        discoverable: Some(Box::new(Bool { value: false })),
        discovering: Some(Box::new(Bool { value: false })),
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(test_state.clone(), closed_state_update))?;

    Ok(())
}

// Tests that "list_devices" returns devices from a host's cache.
async fn test_list_devices(test_state: HostTestPtr) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(vec![], devices)?;

    // Wait for all fake devices to be discovered.
    // TODO(NET-1457): Add support for setting these up programmatically instead of hardcoding
    // them. The fake HCI driver currently sets up one LE and one BR/EDR device.
    await!(test_state.read().host_proxy.start_discovery())?;
    let expected_le = RemoteDeviceExpectation {
        address: Some("00:00:00:00:00:01".to_string()),
        technology: Some(TechnologyType::LowEnergy),
        ..Default::default()
    };
    let expected_bredr = RemoteDeviceExpectation {
        address: Some("00:00:00:00:00:02".to_string()),
        technology: Some(TechnologyType::Classic),
        ..Default::default()
    };
    await!(HostTest::on_device_update(test_state.clone(), None, expected_le.clone()))?;
    await!(HostTest::on_device_update(test_state.clone(), None, expected_bredr.clone()))?;

    // List the host's devices
    let devices = await!(test_state.read().host_proxy.list_devices())?;

    // Both fake devices should be in the map.
    expect_eq!(2, devices.len())?;
    expect_remote_device(&test_state, "00:00:00:00:00:01", &expected_le)?;
    expect_remote_device(&test_state, "00:00:00:00:00:02", &expected_bredr)?;
    Ok(())
}

fn new_le_bond_data(id: &str, address: &str, has_ltk: bool) -> BondingData {
    BondingData {
        identifier: id.to_string(),
        local_address: "AA:BB:CC:DD:EE:FF".to_string(),
        name: None,
        le: Some(Box::new(LeData {
            address: address.to_string(),
            address_type: AddressType::LeRandom,
            connection_parameters: None,
            services: vec![],
            ltk: if has_ltk {
                Some(Box::new(Ltk {
                    key: Key {
                        security_properties: SecurityProperties {
                            authenticated: true,
                            secure_connections: false,
                            encryption_key_size: 16,
                        },
                        value: [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16],
                    },
                    key_size: 16,
                    ediv: 1,
                    rand: 2,
                }))
            } else {
                None
            },
            irk: None,
            csrk: None,
        })),
        bredr: None,
    }
}

fn add_bonds(
    state: &HostTestPtr,
    mut bonds: Vec<BondingData>,
) -> impl Future<Output = Result<(Status), Error>> {
    state.read().host_proxy.add_bonded_devices(&mut bonds.iter_mut()).err_into()
}

const TEST_ID1: &str = "1234";
const TEST_ID2: &str = "2345";
const TEST_ADDR1: &str = "01:02:03:04:05:06";
const TEST_ADDR2: &str = "06:05:04:03:02:01";

// Tests initializing bonded LE devices.
async fn test_add_bonded_devices_success(test_state: HostTestPtr) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(vec![], devices)?;

    let bond_data1 = new_le_bond_data(TEST_ID1, TEST_ADDR1, true /* has LTK */);
    let bond_data2 = new_le_bond_data(TEST_ID2, TEST_ADDR2, true /* has LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data1, bond_data2]))?;
    expect_true!(status.error.is_none())?;

    // We should receive notifications for the newly added devices.
    let expected1 = RemoteDeviceExpectation {
        address: Some(TEST_ADDR1.to_string()),
        technology: Some(TechnologyType::LowEnergy),
        bonded: Some(true),
        ..Default::default()
    };
    let expected2 = RemoteDeviceExpectation {
        address: Some(TEST_ADDR2.to_string()),
        technology: Some(TechnologyType::LowEnergy),
        bonded: Some(true),
        ..Default::default()
    };
    await!(HostTest::on_device_update(test_state.clone(), None, expected1))?;
    await!(HostTest::on_device_update(test_state.clone(), None, expected2))?;

    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(2, devices.len())?;
    expect_true!(devices.iter().any(|dev| dev.address == TEST_ADDR1))?;
    expect_true!(devices.iter().any(|dev| dev.address == TEST_ADDR2))?;

    Ok(())
}

async fn test_add_bonded_devices_no_ltk_fails(test_state: HostTestPtr) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(vec![], devices)?;

    // Inserting a bonded device without a LTK should fail.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR1, false /* no LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_some())?;

    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(vec![], devices)?;

    Ok(())
}

async fn test_add_bonded_devices_duplicate_entry(test_state: HostTestPtr) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(vec![], devices)?;

    // Initialize one entry.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR1, true /* with LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_none())?;

    // We should receive a notification for the newly added device.
    let expected = RemoteDeviceExpectation {
        address: Some(TEST_ADDR1.to_string()),
        technology: Some(TechnologyType::LowEnergy),
        bonded: Some(true),
        ..Default::default()
    };
    await!(HostTest::on_device_update(test_state.clone(), None, expected.clone()))?;
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(1, devices.len())?;

    // Adding an entry with the existing id should fail.
    let bond_data = new_le_bond_data(TEST_ID1, TEST_ADDR2, true /* with LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_some())?;

    // Adding an entry with a different ID but existing address should fail.
    let bond_data = new_le_bond_data(TEST_ID2, TEST_ADDR1, true /* with LTK */);
    let status = await!(add_bonds(&test_state, vec![bond_data]))?;
    expect_true!(status.error.is_some())?;

    Ok(())
}

// Tests that adding a list of bonding data with malformed content succeeds for the valid entries
// but reports an error.
async fn test_add_bonded_devices_invalid_entry(test_state: HostTestPtr) -> Result<(), Error> {
    // Devices should be initially empty.
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(vec![], devices)?;

    // Add one entry with no LTK (invalid) and one with (valid). This should create an entry for the
    // valid device but report an error for the invalid entry.
    let no_ltk = new_le_bond_data(TEST_ID1, TEST_ADDR1, false);
    let with_ltk = new_le_bond_data(TEST_ID2, TEST_ADDR2, true);
    let status = await!(add_bonds(&test_state, vec![no_ltk, with_ltk]))?;
    expect_true!(status.error.is_some())?;

    let expected = RemoteDeviceExpectation {
        address: Some(TEST_ADDR2.to_string()),
        technology: Some(TechnologyType::LowEnergy),
        bonded: Some(true),
        ..Default::default()
    };
    await!(HostTest::on_device_update(test_state.clone(), None, expected.clone()))?;
    let devices = await!(test_state.read().host_proxy.list_devices())?;
    expect_eq!(1, devices.len())?;
    expect_remote_device(&test_state, TEST_ADDR2, &expected)?;

    Ok(())
}

// TODO(armansito|xow): Add tests for BR/EDR and dual mode bond data.

fn main() -> Result<(), Error> {
    println!("TEST BEGIN");

    run_test!(test_bd_addr)?;
    run_test!(test_set_local_name)?;
    run_test!(test_discoverable)?;
    run_test!(test_discovery)?;
    run_test!(test_close)?;
    run_test!(test_list_devices)?;
    run_test!(test_add_bonded_devices_success)?;
    run_test!(test_add_bonded_devices_no_ltk_fails)?;
    run_test!(test_add_bonded_devices_duplicate_entry)?;
    run_test!(test_add_bonded_devices_invalid_entry)?;

    println!("ALL TESTS PASSED");
    Ok(())
}
