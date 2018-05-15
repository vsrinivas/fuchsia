extern crate fdio;
extern crate failure;
extern crate rand;
pub extern crate fuchsia_bluetooth as bluetooth;
extern crate fuchsia_async as async;
extern crate fidl_bluetooth_host;
extern crate futures;


use fidl_bluetooth_host::HostProxy;
use std::path::PathBuf;
use std::{thread, time};
use bluetooth::hci;
use bluetooth::host;

mod common;

// The maximum amount of time spent polling
const MAX_POLL_MS: u64 = 30000;
const SLEEP_MS: u64 = 500;
const ITERATIONS: u64 = MAX_POLL_MS / SLEEP_MS;

fn sleep() -> () {
    thread::sleep(time::Duration::from_millis(SLEEP_MS));
}

fn main() {
    let original_hosts = hci::list_host_devices();
    let (hci_device, _) = hci::create_and_bind_device().unwrap();

    // TODO(armansito): Use a device watcher instead of polling.

    let mut bthost = PathBuf::from("");
    let mut retry = 0;
    'find_device: while retry < ITERATIONS {
        retry += 1;
        let new_hosts = hci::list_host_devices();
        for host in new_hosts {
            if !original_hosts.contains(&host) {
                bthost = host;
                break 'find_device;
            }
        }
        sleep();
    }

    // Check a device showed up within an acceptable timeout
    let found_device = common::open_rdwr(&bthost);
    assert!(found_device.is_ok());
    let found_device = found_device.unwrap();

    // Check the right driver is bound to the device
    let driver_name = hci::get_device_driver_name(&found_device).unwrap();
    assert_eq!("bthost", driver_name.to_str().unwrap());

    // Confirm device topology, host is under bt-hci
    let device_topo = fdio::device_get_topo_path(&found_device).unwrap();
    assert!(device_topo.contains("bt-hci"));

    // Open a host channel using an ioctl and check the device is responsive
    let mut executor = async::Executor::new().unwrap();
    let handle = host::open_host_channel(&found_device).unwrap();
    let host = HostProxy::new(async::Channel::from_channel(handle.into()).unwrap());
    let info = executor.run_singlethreaded(host.get_info());
    assert!(info.is_ok());
    if let Ok(info) = info {
        assert_eq!("00:00:00:00:00:00", info.address);
    }

    // Remove the bt-hci device
    hci::destroy_device(&hci_device);

    // Check the host driver is also destroyed
    let post_destroy_hosts = hci::list_host_devices();
    let mut device_found = true;
    let mut retry = 0;
    while retry < ITERATIONS {
        retry += 1;
        let new_hosts = hci::list_host_devices();
        if !new_hosts.contains(&bthost) {
            device_found = false;
            break;
        }
        sleep();
    }
    assert!(!device_found);
}
