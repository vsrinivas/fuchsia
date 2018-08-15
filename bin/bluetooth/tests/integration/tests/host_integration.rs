// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(
    arbitrary_self_types,
    async_await,
    await_macro,
    futures_api,
    pin
)]

use fidl_fuchsia_bluetooth::Bool;
use fidl_fuchsia_bluetooth_control::{AdapterInfo, AdapterState};
use fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy};
use fuchsia_async::{self as fasync, TimeoutExt};
use fuchsia_bluetooth::error::Error as BtError;
use fuchsia_bluetooth::util::clone_host_state;
use fuchsia_bluetooth::{fake_hci::FakeHciDevice, host};
use fuchsia_vfs_watcher::{WatchEvent as VfsWatchEvent, Watcher as VfsWatcher};
use fuchsia_zircon::DurationNum;
use futures::{future, task, Future, FutureExt, Poll, TryFutureExt, TryStreamExt};

use failure::{Error, ResultExt};
use parking_lot::RwLock;
use slab::Slab;
use std::borrow::Borrow;
use std::fs::File;
use std::io::{self, Write};
use std::path::PathBuf;
use std::sync::Arc;

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

    // Tasks that are interested in being woken up when the adapter state changes.
    host_state_tasks: Slab<task::Waker>,
}

impl HostTest {
    fn new(
        hci: FakeHciDevice, host_path: String, host: HostProxy, info: AdapterInfo,
    ) -> HostTestPtr {
        Arc::new(RwLock::new(HostTest {
            fake_hci_dev: Some(hci),
            host_path: host_path,
            host_proxy: host,
            host_info: info,
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
                        test_state.write().handle_adapter_state_change(state);
                    }
                    // TODO(armansito): handle other events
                    evt => {
                        eprintln!("Unhandled event: {:?}", evt);
                    }
                }
                future::ready(Ok(()))
            }).err_into()
    }

    // Returns a Future that resolves when the bt-host transitions to a state that matches the
    // valid fields of |target|. For example, if |target_state| is
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
        test_state: HostTestPtr, target_state: AdapterState,
    ) -> impl Future<Output = Result<AdapterState, Error>> {
        AdapterStateFuture::new(test_state, target_state)
            .on_timeout(TIMEOUT.seconds().after_now(), || {
                Err(BtError::new("timed out waiting for adapter state change").into())
            })
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

    // Handle the OnAdapterStateChanged event.
    fn handle_adapter_state_change(&mut self, state_change: AdapterState) {
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

    fn notify_host_state_changed(&mut self) {
        for task in &self.host_state_tasks {
            task.1.wake();
        }
        self.host_state_tasks.clear()
    }
}

// Returns the value of |field| in |delta| if it's valid. Otherwise returns the value in |base|.
macro_rules! updated_field {
    ($base:ident, $delta:ident, $field:ident) => {
        match $delta.$field {
            None => $base.$field,
            value => value,
        }
    };
}

// Applies |delta| to |base|.
fn apply_delta(base: AdapterState, delta: AdapterState) -> AdapterState {
    AdapterState {
        local_name: updated_field!(base, delta, local_name),
        discoverable: updated_field!(base, delta, discoverable),
        discovering: updated_field!(base, delta, discovering),
        local_service_uuids: updated_field!(base, delta, local_service_uuids),
    }
}

macro_rules! compare_field {
    ($base:ident, $target:ident, $field:ident) => {
        match $target.$field {
            None => None,
            ref value => Some(*value == $base.$field),
        }
    };
}

macro_rules! compare_fields {
    ($base:ident, $target:ident, [$($field:ident),*]) => {{
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

// Returns true if all valid fields of |target| match their equivalents in |base|. Returns false if
// there are any mismatches or if all fields of |target| are None.
fn compare_adapter_states(base: &AdapterState, target: &AdapterState) -> bool {
    compare_fields!(
        base,
        target,
        [local_name, discoverable, discovering, local_service_uuids]
    )
}

struct AdapterStateFuture {
    test_state: HostTestPtr,
    waker_key: Option<usize>,

    // The expected adapter state.
    target_host_state: AdapterState,
}

impl AdapterStateFuture {
    fn new(test: HostTestPtr, target_state: AdapterState) -> AdapterStateFuture {
        AdapterStateFuture {
            test_state: test,
            waker_key: None,
            target_host_state: target_state,
        }
    }
}

impl std::marker::Unpin for AdapterStateFuture {}

#[must_use = "futures do nothing unless polled"]
impl Future for AdapterStateFuture {
    type Output = Result<AdapterState, Error>;

    fn poll(mut self: std::pin::Pin<&mut Self>, lw: &task::LocalWaker) -> Poll<Self::Output> {
        // Remove existing waker, if any.
        if let Some(key) = self.waker_key {
            self.test_state.write().remove_task(key);
            self.waker_key = None;
        }

        let states_match: bool = match &self.test_state.read().host_info.state {
            None => false,
            Some(state) => compare_adapter_states(state.borrow(), &self.target_host_state),
        };
        if states_match {
            Poll::Ready(Ok(clone_host_state(&self.target_host_state)))
        } else {
            let key = self.test_state.write().store_task(lw.clone().into_waker());
            self.waker_key = Some(key);
            Poll::Pending
        }
    }
}

// Returns a Future that resolves when a bt-host device gets added under the given topological
// path.
async fn watch_for_new_host_helper(
    mut watcher: VfsWatcher, parent_topo_path: String,
) -> Result<(File, PathBuf), Error> {
    while let Some(msg) = await!(watcher.try_next())? {
        match msg.event {
            VfsWatchEvent::ADD_FILE => {
                let path = PathBuf::from(format!(
                    "{}/{}",
                    BT_HOST_DIR,
                    msg.filename.to_string_lossy()
                ));
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
    fut.on_timeout(TIMEOUT.seconds().after_now(), move || {
        Err(BtError::new(msg).into())
    })
}

fn watch_for_new_host(
    watcher: VfsWatcher, fake_hci_topo_path: String,
) -> impl Future<Output = Result<(File, PathBuf), Error>> {
    timeout(
        watch_for_new_host_helper(watcher, fake_hci_topo_path),
        "timed out waiting for bt-host",
    )
}

fn wait_for_host_removal(
    watcher: VfsWatcher, path: String,
) -> impl Future<Output = Result<(), Error>> {
    timeout(
        wait_for_host_removal_helper(watcher, path),
        "timed out waiting for bt-host removal",
    )
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

    Ok(HostTest::new(
        fake_hci,
        path.to_string_lossy().to_string(),
        host,
        info,
    ))
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
    await!(wait_for_host_removal(
        watcher,
        host_test.read().host_path.clone()
    ))?;

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

macro_rules! expect_eq {
    ($expected:expr, $actual:expr) => {{
        let expected_val = $expected;
        let actual_val = $actual;
        if expected_val == actual_val {
            Ok(())
        } else {
            Err(BtError::new(&format!(
                "failed - expected '{}', found: '{}'",
                expected_val, actual_val
            )).into())
        }
    }};
}

// ========= Test Cases =========

// Tests that the device address is 0.
async fn test_bd_addr(test_state: HostTestPtr) -> Result<(), Error> {
    let info = await!(test_state.read().host_proxy.get_info())
        .map_err(|_| BtError::new("failed to read adapter info"))?;
    expect_eq!("00:00:00:00:00:00", info.address)
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
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        state_change
    ))?;

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
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        discoverable_state
    ))?;

    // Disable discoverable mode
    await!(test_state.read().host_proxy.set_discoverable(false))?;
    let non_discoverable_state = AdapterState {
        local_name: None,
        discoverable: Some(Box::new(Bool { value: false })),
        discovering: None,
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        non_discoverable_state
    ))?;

    Ok(())
}

// Tests that host state updates when discovery is started and stopped.
// TODO(armansito): Test for remote device events.
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
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        discovering_state
    ))?;

    // Stop discovery. "discovering" should get set to false.
    await!(test_state.read().host_proxy.stop_discovery())?;
    let not_discovering_state = AdapterState {
        local_name: None,
        discoverable: None,
        discovering: Some(Box::new(Bool { value: false })),
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        not_discovering_state
    ))?;

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
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        active_state
    ))?;

    // Close should cancel these procedures.
    test_state.read().host_proxy.close()?;
    let closed_state_update = AdapterState {
        local_name: None,
        discoverable: Some(Box::new(Bool { value: false })),
        discovering: Some(Box::new(Bool { value: false })),
        local_service_uuids: None,
    };
    await!(HostTest::on_adapter_state_change(
        test_state.clone(),
        closed_state_update
    ))?;

    Ok(())
}

fn main() -> Result<(), Error> {
    println!("TEST BEGIN");

    run_test!(test_bd_addr)?;
    run_test!(test_set_local_name)?;
    run_test!(test_discoverable)?;
    run_test!(test_discovery)?;
    run_test!(test_close)?;

    println!("ALL TESTS PASSED");
    Ok(())
}
