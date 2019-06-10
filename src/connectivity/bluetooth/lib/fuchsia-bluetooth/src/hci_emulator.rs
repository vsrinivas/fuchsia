// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, format_err, Error},
    fidl_fuchsia_bluetooth_test::HciEmulatorProxy,
    fidl_fuchsia_device::ControllerSynchronousProxy,
    fidl_fuchsia_device_test::{
        DeviceSynchronousProxy, RootDeviceSynchronousProxy, CONTROL_DEVICE, MAX_DEVICE_NAME_LEN,
    },
    fidl_fuchsia_hardware_bluetooth::EmulatorProxy,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_syslog::fx_log_err,
    fuchsia_vfs_watcher::{WatchEvent as VfsWatchEvent, Watcher as VfsWatcher},
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::{
        fs::{File, OpenOptions},
        path::{Path, PathBuf},
    },
};

const FAKE_HCI_DRIVER_PATH: &str = "/system/driver/bt-hci-fake.so";
const EMULATOR_DEVICE_DIR: &str = "/dev/class/bt-emulator";

fn watch_timeout() -> zx::Duration {
    zx::Duration::from_seconds(10)
}

/// Represents a bt-hci device emulator. Instances of this type can be used manage the bt-hci-fake
/// driver within the test device hierarchy. The associated driver instance gets unbound and all
/// bt-hci and bt-emulator device instances destroyed when a Emulator goes out of scope.
pub struct Emulator {
    dev: TestDevice,
    emulator: HciEmulatorProxy,
}

impl Emulator {
    /// Publishes a new fake bt-hci device and constructs a Emulator with it.
    pub async fn new(name: &str) -> Result<Emulator, Error> {
        let dev = TestDevice::create(name)?;
        let emulator = await!(dev.bind())?;
        Ok(Emulator { dev: dev, emulator: emulator })
    }

    /// Returns a reference to the underlying file.
    pub fn file(&self) -> &File {
        &self.dev.0
    }

    /// Returns a reference to the fuchsia.bluetooth.test.HciEmulator protocol proxy.
    pub fn emulator(&self) -> &HciEmulatorProxy {
        &self.emulator
    }
}

// Represents the test device. Destroys the underlying device when it goes out of scope.
struct TestDevice(File);

impl TestDevice {
    // Creates a new device as a child of the root test device. This device will act as the parent
    // of our fake HCI device. If successful, `name` will act as the final fragment of the device
    // path, for example "/dev/test/test/{name}".
    fn create(name: &str) -> Result<TestDevice, Error> {
        if name.len() > (MAX_DEVICE_NAME_LEN as usize) {
            bail!(
                "Device name '{}' too long (must be {} or fewer chars)",
                name,
                MAX_DEVICE_NAME_LEN
            );
        }

        // Connect to the test control device and obtain a channel to the RootDevice capability.
        let control_dev = open_rdwr(CONTROL_DEVICE)?;
        let mut root_device = RootDeviceSynchronousProxy::new(fdio::clone_channel(&control_dev)?);

        // Create a device with the requested name.
        let (status, path) = root_device.create_device(name, zx::Time::INFINITE)?;
        zx::Status::ok(status)?;
        let path = path.ok_or(format_err!("RootDevice.CreateDevice returned null path"))?;

        // Open the device that was just created.
        Ok(TestDevice(open_rdwr(&path)?))
    }

    // Send the test device a destroy message which will unbind the driver.
    fn destroy(&mut self) -> Result<(), Error> {
        let channel = fdio::clone_channel(&self.0)?;
        let mut device = DeviceSynchronousProxy::new(channel);
        Ok(device.destroy()?)
    }

    // Bind the bt-hci-fake driver and obtain the HciEmulator protocol channel.
    async fn bind(&self) -> Result<HciEmulatorProxy, Error> {
        let channel = fdio::clone_channel(&self.0)?;
        let mut controller = ControllerSynchronousProxy::new(channel);
        let status = controller.bind(FAKE_HCI_DRIVER_PATH, zx::Time::INFINITE)?;
        zx::Status::ok(status)?;

        // Wait until a bt-emulator device gets published under our test device.
        let topo_path = fdio::device_get_topo_path(&self.0)?;
        let emulator_dev = await!(watch_for_emulator_device(topo_path)
            .on_timeout(watch_timeout().after_now(), || Err(format_err!(
                "could not find bt-emulator device"
            ))))?;

        // Connect to the bt-emulator device.
        let channel = fdio::clone_channel(&emulator_dev)?;
        let emulator = EmulatorProxy::new(fasync::Channel::from_channel(channel)?);

        // Open a HciEmulator protocol channel.
        let (proxy, remote) = zx::Channel::create()?;
        emulator.open(remote)?;
        Ok(HciEmulatorProxy::new(fasync::Channel::from_channel(proxy)?))
    }
}

impl Drop for TestDevice {
    fn drop(&mut self) {
        if let Err(e) = self.destroy() {
            fx_log_err!("error while destroying test device: {:?}", e);
        }
    }
}

// Asynchronously returns the first available bt-emulator device under the given topological path.
// The returned Future does not terminates until a bt-emulator device is found under the requested
// topology.
async fn watch_for_emulator_device(parent_topo_path: String) -> Result<File, Error> {
    let dir = File::open(&EMULATOR_DEVICE_DIR)?;
    let mut watcher = VfsWatcher::new(&dir)?;
    while let Some(msg) = await!(watcher.try_next())? {
        match msg.event {
            VfsWatchEvent::EXISTING | VfsWatchEvent::ADD_FILE => {
                let path = PathBuf::from(format!(
                    "{}/{}",
                    EMULATOR_DEVICE_DIR,
                    msg.filename.to_string_lossy()
                ));
                let dev = open_rdwr(&path)?;
                let topo_path = fdio::device_get_topo_path(&dev)?;
                if topo_path.starts_with(parent_topo_path.as_str()) {
                    return Ok(dev);
                }
            }
            _ => (),
        }
    }
    unreachable!();
}

fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new().read(true).write(true).open(path).map_err(|e| e.into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_bluetooth_test::{EmulatorError, EmulatorSettings};

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_publish() {
        let fake_dev = await!(Emulator::new("publish-test-0")).unwrap();

        let settings = EmulatorSettings {
            address: None,
            hci_config: None,
            extended_advertising: None,
            acl_buffer_settings: None,
            le_acl_buffer_settings: None,
        };

        // TODO(BT-229): Test for success when publish is implemented.
        let result = await!(fake_dev.emulator().publish(settings))
            .expect("Failed to send Publish message to emulator device");
        assert_eq!(Err(EmulatorError::HciAlreadyPublished), result);
    }
}
