// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error, ResultExt},
    fidl_fuchsia_bluetooth_host::HostProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{fake_hci::FakeHciDevice, hci, host},
    fuchsia_vfs_watcher::{self as vfs_watcher, WatchEvent, WatchMessage},
    futures::{Stream, StreamExt, TryStreamExt},
    pin_utils::pin_mut,
    std::{
        fs::File,
        io,
        path::{Path, PathBuf},
        thread, time,
    },
};

// The maximum amount of time spent polling
const MAX_POLL_MS: u64 = 30000;
const SLEEP_MS: u64 = 500;
const ITERATIONS: u64 = MAX_POLL_MS / SLEEP_MS;

fn sleep() -> () {
    thread::sleep(time::Duration::from_millis(SLEEP_MS));
}

async fn added_host(msg: WatchMessage) -> Result<Option<PathBuf>, io::Error> {
    let path = Path::new(host::BT_HOST_DIR).join(&msg.filename);
    Ok(match msg.event {
        WatchEvent::EXISTING | WatchEvent::ADD_FILE => Some(path),
        _ => None,
    })
}

/// Watch the VFS for host adapter devices being added or removed, and produce
/// a stream of AdapterEvent messages
fn watch_hosts() -> impl Stream<Item = Result<PathBuf, Error>> {
    let dev = File::open(&host::BT_HOST_DIR);
    let watcher = vfs_watcher::Watcher::new(&dev.unwrap())
        .expect("Cannot open vfs watcher for bt-host device path");
    watcher.try_filter_map(added_host).map_err(|e| e.into())
}

async fn watch_for_host(original_hosts: Vec<PathBuf>) -> Result<PathBuf, Error> {
    let stream = watch_hosts();
    pin_mut!(stream);
    while let Some(h) = await!(stream.next()) {
        let host = h?;
        if !original_hosts.contains(&host) {
            return Ok(host);
        }
    }
    Err(err_msg("Not found"))
}

// Tests that creating and destroying a fake HCI device binds and unbinds the bt-host driver.
pub async fn lifecycle_test(_: ()) -> Result<(), Error> {
    let original_hosts = host::list_host_devices();
    let fake_hci = FakeHciDevice::new("bt-hci-integration-lifecycle")?;
    let bthost = await!(watch_for_host(original_hosts))?;

    // Check a device showed up within an acceptable timeout
    let found_device = hci::open_rdwr(&bthost);
    assert!(found_device.is_ok());
    let found_device = found_device?;

    // Check the right driver is bound to the device
    let driver_name = hci::get_device_driver_name(&found_device)?;
    assert_eq!("bt_host", driver_name);

    // Confirm device topology, host is under bt-hci
    let device_topo = fdio::device_get_topo_path(&found_device)?;
    assert!(device_topo.contains("bt-hci"));

    // Open a host channel using a fidl call and check the device is responsive
    let handle = host::open_host_channel(&found_device)?;
    let host = HostProxy::new(fasync::Channel::from_channel(handle.into())?);
    let info = await!(host.get_info())
        .context("Is bt-gap running? If so, try stopping it and re-running these tests")?;
    assert_eq!("00:00:00:00:00:00", info.address);

    // Remove the bt-hci device
    drop(fake_hci);

    // Check the host driver is also destroyed
    let _post_destroy_hosts = host::list_host_devices();
    let mut device_found = true;
    let mut retry = 0;
    while retry < ITERATIONS {
        retry += 1;
        let new_hosts = host::list_host_devices();
        if !new_hosts.contains(&bthost) {
            device_found = false;
            break;
        }
        sleep();
    }
    assert!(!device_found);

    Ok(())
}
