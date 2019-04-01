// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fuchsia_async as fasync,
    fuchsia_bluetooth::{fake_hci::FakeHciDevice, hci, host},
    std::{path::PathBuf, thread, time},
};

// The maximum amount of time spent polling
const MAX_POLL_MS: u64 = 30000;
const SLEEP_MS: u64 = 500;
const ITERATIONS: u64 = MAX_POLL_MS / SLEEP_MS;

fn sleep() -> () {
    thread::sleep(time::Duration::from_millis(SLEEP_MS));
}

// Tests that creating and destroying a fake HCI device binds and unbinds the bt-host driver.
pub fn lifecycle_test() -> Result<(), Error> {
    let original_hosts = host::list_host_devices();
    let fake_hci = FakeHciDevice::new("bt-hci-integration-lifecycle")?;

    // TODO(armansito): Use a device watcher instead of polling.

    let mut bthost = PathBuf::from("");
    let mut retry = 0;
    'find_device: while retry < ITERATIONS {
        retry += 1;
        let new_hosts = host::list_host_devices();
        for host in new_hosts {
            if !original_hosts.contains(&host) {
                bthost = host;
                break 'find_device;
            }
        }
        sleep();
    }

    // Check a device showed up within an acceptable timeout
    let found_device = hci::open_rdwr(&bthost);
    assert!(found_device.is_ok());
    let found_device = found_device.unwrap();

    // Check the right driver is bound to the device
    let driver_name = hci::get_device_driver_name(&found_device).unwrap();
    assert_eq!("bt_host", driver_name);

    // Confirm device topology, host is under bt-hci
    let device_topo = fdio::device_get_topo_path(&found_device).unwrap();
    assert!(device_topo.contains("bt-hci"));

    // Open a host channel using a fidl call and check the device is responsive
    let mut executor = fasync::Executor::new().unwrap();
    let handle = host::open_host_channel(&found_device).unwrap();
    let host = HostProxy::new(fasync::Channel::from_channel(handle.into()).unwrap());
    let info = executor.run_singlethreaded(host.get_info());
    assert!(info.is_ok());
    if let Ok(info) = info {
        assert_eq!("00:00:00:00:00:00", info.address);
    }

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
