// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{EMULATOR_DEVICE_DIR, EMULATOR_DRIVER_PATH},
    failure::{bail, format_err, Error},
    fidl_fuchsia_bluetooth_test::{EmulatorSettings, HciEmulatorProxy},
    fidl_fuchsia_device::ControllerSynchronousProxy,
    fidl_fuchsia_device_test::{
        DeviceSynchronousProxy, RootDeviceSynchronousProxy, CONTROL_DEVICE, MAX_DEVICE_NAME_LEN,
    },
    fidl_fuchsia_hardware_bluetooth::EmulatorProxy,
    fuchsia_async::{self as fasync, TimeoutExt},
    fuchsia_syslog::{fx_log_err, fx_log_info},
    fuchsia_vfs_watcher::{WatchEvent as VfsWatchEvent, Watcher as VfsWatcher},
    fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::{
        fs::{File, OpenOptions},
        path::{Path, PathBuf},
    },
};

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
    /// Publish a new bt-emulator device and return a handle to it. No corresponding bt-hci device
    /// will be published; to do so it must be explicitly configured and created with a call to
    /// `publish()`
    pub async fn create(name: &str) -> Result<Emulator, Error> {
        let dev = TestDevice::create(name)?;
        let emulator = await!(dev.bind())?;
        Ok(Emulator { dev: dev, emulator: emulator })
    }

    /// Publish a bt-emulator and a bt-hci device using the default emulator settings.
    pub async fn create_and_publish(name: &str) -> Result<Emulator, Error> {
        let fake_dev = await!(Emulator::create(name))?;
        let default_settings = EmulatorSettings {
            address: None,
            hci_config: None,
            extended_advertising: None,
            acl_buffer_settings: None,
            le_acl_buffer_settings: None,
        };
        let result = await!(fake_dev.emulator().publish(default_settings))?;
        match result {
            Ok(()) => Ok(fake_dev),
            Err(e) => Err(format_err!("failed to publish bt-hci device: {:#?}", e)),
        }
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

        // Create a watcher for the emulator device before binding the driver so that we can watch
        // for addition events.
        let status = controller.bind(EMULATOR_DRIVER_PATH, zx::Time::INFINITE)?;
        zx::Status::ok(status)?;

        // Wait until a bt-emulator device gets published under our test device.
        let topo_path = fdio::device_get_topo_path(&self.0)?;
        let emulator_dev = await!(watch_for_device(EMULATOR_DEVICE_DIR, &topo_path))?;

        // Connect to the bt-emulator device.
        let channel = fdio::clone_channel(&emulator_dev.file)?;
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

// Represents a device file that is opened when waiting for device creation.
struct DeviceFile {
    file: File,

    // The path of the device in the namespace. This is an informational field that used to watch
    // for device removal in the tests below (we use this since it's not possible to obtain the
    // topological path of a device that got removed).
    path: PathBuf,

    // Topological path used to verify that the device is published under the correct hierarchy.
    topo_path: PathBuf,
}

// Helper macro for waiting on a Future with a timeout.
macro_rules! await_timeout {
    ($fut:expr) => {
        await!($fut.on_timeout(watch_timeout().after_now(), || Err(format_err!(
            "timed out waiting for device"
        ))))
    };
}

// Helper functions for asynchronously waiting for existing and added devices. These are used by
// production code and unit tests below.

async fn watch_for_device<'a>(
    dir_path: &'a str,
    parent_topo_path: &'a str,
) -> Result<DeviceFile, Error> {
    let dir = File::open(dir_path)?;
    let mut watcher = VfsWatcher::new(&dir)?;
    await_timeout!(watch_for_device_helper(
        &mut watcher,
        dir_path,
        parent_topo_path,
        vec![VfsWatchEvent::EXISTING, VfsWatchEvent::ADD_FILE]
    ))
}

async fn watch_for_device_helper<'a>(
    watcher: &'a mut VfsWatcher,
    dir_path: &'a str,
    parent_topo_path: &'a str,
    events: Vec<VfsWatchEvent>,
) -> Result<DeviceFile, Error> {
    while let Some(msg) = await!(watcher.try_next())? {
        if events.contains(&msg.event) {
            let dev = open_dev(&msg.filename.to_string_lossy(), dir_path)?;
            if dev.topo_path.starts_with(parent_topo_path) {
                fx_log_info!("found device: {:#?}", dev.path);
                return Ok(dev);
            }
        }
    }
    unreachable!();
}

fn open_dev(filename: &str, dir: &str) -> Result<DeviceFile, Error> {
    let path = PathBuf::from(format!("{}/{}", dir, filename));
    let dev = open_rdwr(&path)?;
    let topo_path = fdio::device_get_topo_path(&dev)?;
    Ok(DeviceFile { file: dev, path: path, topo_path: PathBuf::from(topo_path) })
}

fn open_rdwr<P: AsRef<Path>>(path: P) -> Result<File, Error> {
    OpenOptions::new().read(true).write(true).open(path).map_err(|e| e.into())
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::constants::HCI_DEVICE_DIR, fidl_fuchsia_bluetooth_test::EmulatorError,
        futures::Future,
    };

    fn default_settings() -> EmulatorSettings {
        EmulatorSettings {
            address: None,
            hci_config: None,
            extended_advertising: None,
            acl_buffer_settings: None,
            le_acl_buffer_settings: None,
        }
    }

    // Waits until a device with the given `path` gets removed.
    async fn watch_for_removal<'a>(
        watcher: &'a mut VfsWatcher,
        dir: &'a str,
        path: &'a PathBuf,
    ) -> Result<(), Error> {
        while let Some(msg) = await!(watcher.try_next())? {
            match msg.event {
                VfsWatchEvent::REMOVE_FILE => {
                    let dev_path =
                        PathBuf::from(format!("{}/{}", dir, &msg.filename.to_string_lossy()));
                    if dev_path == *path {
                        return Ok(());
                    }
                }
                _ => (),
            }
        }
        unreachable!();
    }

    fn watch_for_existing<'a>(
        watcher: &'a mut VfsWatcher,
        dir: &'a str,
        parent_topo_path: &'a str,
    ) -> impl Future<Output = Result<DeviceFile, Error>> + 'a {
        watch_for_device_helper(watcher, dir, parent_topo_path, vec![VfsWatchEvent::EXISTING])
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_publish_lifecycle() {
        // Create watchers for bt-hci and bt-emulator device creation.
        let hci_dir = File::open(HCI_DEVICE_DIR).expect("Failed to open bt-hci device dir");
        let emul_dir =
            File::open(EMULATOR_DEVICE_DIR).expect("Failed to open bt-emulator device dir");

        // We use these watchers to verify the addition and removal of these devices as tied to the
        // lifetime of the Emulator instance we create below.
        let mut hci_watcher: VfsWatcher;
        let mut emul_watcher: VfsWatcher;

        let mut hci_dev: DeviceFile;
        let mut emul_dev: DeviceFile;

        {
            let fake_dev =
                await!(Emulator::create("publish-test-0")).expect("Failed to construct Emulator");
            let topo_path = fdio::device_get_topo_path(&fake_dev.dev.0)
                .expect("Failed to obtain topological path for Emulator");

            // A bt-emulator device should already exist by now.
            emul_watcher =
                VfsWatcher::new(&emul_dir).expect("Failed to create bt-emulator directory watcher");
            emul_dev = await_timeout!(watch_for_existing(
                &mut emul_watcher,
                EMULATOR_DEVICE_DIR,
                &topo_path
            ))
            .expect("Expected bt-emulator device to be published");

            // Send a publish message to the device. This call should succeed and result in a new
            // bt-hci device. (Note: it is important for `hci_watcher` to get constructed here since
            // our expectation is based on the `ADD_FILE` event).
            hci_watcher =
                VfsWatcher::new(&hci_dir).expect("Failed to create bt-hci directory watcher");
            let result = await!(fake_dev.emulator().publish(default_settings()))
                .expect("Failed to send Publish message to emulator device");
            assert!(result.is_ok());
            hci_dev = await_timeout!(watch_for_device_helper(
                &mut hci_watcher,
                HCI_DEVICE_DIR,
                &topo_path,
                vec![VfsWatchEvent::ADD_FILE]
            ))
            .expect("Expected a bt-hci device to be published");

            // Once a device is published, it should not be possible to publish again while the
            // HciEmulator channel is open.
            let result = await!(fake_dev.emulator().publish(default_settings()))
                .expect("Failed to send second Publish message to emulator device");
            assert_eq!(Err(EmulatorError::HciAlreadyPublished), result);
        }

        // Both devices should be destroyed when `fake_dev` gets dropped.
        let _ = await_timeout!(watch_for_removal(&mut hci_watcher, HCI_DEVICE_DIR, &hci_dev.path))
            .expect("Expected the bt-hci device to get removed");
        let _ = await_timeout!(watch_for_removal(
            &mut emul_watcher,
            EMULATOR_DEVICE_DIR,
            &emul_dev.path
        ))
        .expect("Expected the bt-emulator device to get removed");
    }
}
