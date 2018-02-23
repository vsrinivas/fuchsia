#[macro_use]
extern crate fdio;
#[macro_use]
extern crate failure;
extern crate rand;
pub extern crate fuchsia_bluetooth as bluetooth;

use std::path::PathBuf;
use bluetooth::hci;

mod common;

#[test]
fn bt_host_lifecycle() {
    let original_hosts = hci::list_host_devices();
    let hci_device = hci::create_and_bind_device().unwrap();

    let mut bthost = PathBuf::from("");
    let mut retry = 0;
    'find_device: while retry < 1000 {
        retry += 1;
        let new_hosts = hci::list_host_devices();
        for host in new_hosts {
            if !original_hosts.contains(&host) {
                bthost = host;
                break 'find_device;
            }
        }
    }

    // Check a device showed up within an acceptable timeout
    let found_device = common::open_rdwr(&bthost);
    assert!(found_device.is_ok());
    let found_device = found_device.unwrap();

    // Check the right driver is bound to the device
    let driver_name = hci::get_device_driver_name(&found_device).unwrap();
    assert_eq!("bthost", driver_name.to_str().unwrap());

    // Confirm device topology, host is under bt-hci
    let device_topo = hci::get_device_driver_topo(&found_device).unwrap();
    assert!(device_topo.to_string_lossy().contains("bt-hci"));

    // Remove the bt-hci device
    hci::destroy_device(&hci_device);

    // Check the host driver is also destroyed
    let post_destroy_hosts = hci::list_host_devices();
    let mut device_found = true;
    let mut retry = 0;
    while retry < 1000 {
        retry += 1;
        let new_hosts = hci::list_host_devices();
        if !new_hosts.contains(&bthost) {
            device_found = false;
            break;
        }
    }
    assert!(!device_found);
}
