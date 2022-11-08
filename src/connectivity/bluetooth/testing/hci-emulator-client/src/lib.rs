// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    bt_device_watcher::{DeviceFile, DeviceWatcher, WatchFilter},
    device_watcher, fdio,
    fidl::endpoints::Proxy,
    fidl_fuchsia_bluetooth_test::{EmulatorSettings, HciEmulatorProxy},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_bluetooth::{EmulatorProxy, VirtualControllerProxy},
    fidl_fuchsia_io as fio,
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        constants::{HOST_DEVICE_DIR, INTEGRATION_TIMEOUT as WATCH_TIMEOUT},
        util::open_rdwr,
    },
    fuchsia_component_test::ScopedInstance,
    fuchsia_fs,
    fuchsia_zircon::{self as zx, HandleBased},
    fuchsia_zircon_status as zx_status,
    futures::TryFutureExt,
    log::error,
    std::{
        fs::File,
        path::{Path, PathBuf},
    },
};

pub mod types;

// 0x30 => fuchsia.platform.BIND_PLATFORM_DEV_DID.BT_HCI_EMULATOR
pub const CONTROL_DEVICE: &str = "/dev/sys/platform/00:00:30/bt_hci_virtual";
pub const EMULATOR_DEVICE_DIR: &str = "/dev/class/bt-emulator";
pub const HCI_DEVICE_DIR: &str = "/dev/class/bt-hci";

/// Represents a bt-hci device emulator. Instances of this type can be used manage the
/// bt-hci-emulator driver within the test device hierarchy. The associated driver instance gets
/// unbound and all bt-hci and bt-emulator device instances destroyed when
/// `destroy_and_wait()` resolves successfully.
/// `destroy_and_wait()` MUST be called for proper clean up of the emulator device.
pub struct Emulator {
    /// This will have a value when the emulator is instantiated and will be reset to None
    /// in `destroy_and_wait()`. This is so the destructor can assert that the TestDevice has been
    /// destroyed.
    dev: Option<TestDevice>,
    emulator: HciEmulatorProxy,
}

impl Emulator {
    /// Returns the default settings.
    // TODO(armansito): Consider defining a library type for EmulatorSettings.
    pub fn default_settings() -> EmulatorSettings {
        EmulatorSettings {
            address: None,
            hci_config: None,
            extended_advertising: None,
            acl_buffer_settings: None,
            le_acl_buffer_settings: None,
            ..EmulatorSettings::EMPTY
        }
    }

    /// Publish a new bt-emulator device and return a handle to it. No corresponding bt-hci device
    /// will be published; to do so it must be explicitly configured and created with a call to
    /// `publish()`. If `realm` is present, the device will be created inside it, otherwise it will
    /// be created using the `/dev` directory in the component's namespace.
    pub async fn create(realm: Option<&ScopedInstance>) -> Result<Emulator, Error> {
        let (dev, emulator) = match realm {
            Some(r) => TestDevice::create_from_realm(r).await,
            None => TestDevice::create().await,
        }
        .context(format!("Error creating test device"))?;
        Ok(Emulator { dev: Some(dev), emulator: emulator })
    }

    /// Publish a bt-emulator and a bt-hci device using the default emulator settings. If `realm`
    /// is present, the device will be created inside it, otherwise it will be created using the
    /// `/dev` directory in the component's namespace.
    pub async fn create_and_publish(realm: Option<&ScopedInstance>) -> Result<Emulator, Error> {
        let fake_dev = Self::create(realm).await?;
        fake_dev.publish(Self::default_settings()).await?;
        Ok(fake_dev)
    }

    /// Sends a publish message to the emulator. This is a convenience method that internally
    /// handles the FIDL binding error.
    pub async fn publish(&self, settings: EmulatorSettings) -> Result<(), Error> {
        let result = self.emulator().publish(settings).await?;
        result.map_err(|e| format_err!("failed to publish bt-hci device: {:#?}", e))
    }

    /// Sends a publish message emulator and returns a Future that resolves when a bt-host device is
    /// published. Note that this requires the bt-host driver to be installed. On success, returns a
    /// `DeviceFile` that represents the bt-host device.
    pub async fn publish_and_wait_for_host(
        &self,
        settings: EmulatorSettings,
    ) -> Result<DeviceFile, Error> {
        let dev_dir = self.dev.as_ref().expect("device should exist by now").dev_directory.as_ref();
        let mut watcher = dev_watcher_maybe_in_namespace(HOST_DEVICE_DIR, dev_dir).await?;
        let _ = self.publish(settings).await?;
        let topo = PathBuf::from(fdio::device_get_topo_path(self.file())?);
        watcher.watch_new(&topo, WatchFilter::AddedOrExisting).await
    }

    /// Sends the test device a destroy message which will unbind the driver.
    /// This will wait for the test device to be unpublished from devfs.
    pub async fn destroy_and_wait(&mut self) -> Result<(), Error> {
        let fake_dev = self.dev.take();
        fake_dev
            .expect("attempted to destroy an already destroyed emulator device")
            .destroy_and_wait()
            .await
    }

    /// Returns a reference to the underlying file.
    pub fn file(&self) -> &File {
        &self.dev.as_ref().expect("emulator device accessed after it was destroyed!").file
    }

    /// Returns a reference to the fuchsia.bluetooth.test.HciEmulator protocol proxy.
    pub fn emulator(&self) -> &HciEmulatorProxy {
        &self.emulator
    }
}

impl Drop for Emulator {
    fn drop(&mut self) {
        if self.dev.is_some() {
            error!("Did not call destroy() on Emulator");
        }
    }
}

// Represents the test device. `destroy()` MUST be called explicitly to remove the device.
// The device will be removed asynchronously so the caller cannot rely on synchronous
// execution of destroy() to know about device removal. Instead, the caller should watch for the
// device path to be removed.
struct TestDevice {
    file: File,
    /// If present, the test device was opened relative to a ScopedInstance's existing `/dev` dir
    pub dev_directory: Option<fio::DirectoryProxy>,
}

impl TestDevice {
    // Creates a new device as a child of the emulator controller device and obtain the HciEmulator
    // protocol channel.
    async fn create() -> Result<(TestDevice, HciEmulatorProxy), Error> {
        let control_dev =
            open_rdwr(CONTROL_DEVICE).context(format!("Error opening file {}", CONTROL_DEVICE))?;
        let controller_channel = zx::Channel::from(fdio::transfer_fd(control_dev)?);
        Self::create_internal(fasync::Channel::from_channel(controller_channel)?, None).await
    }

    // Like create, but for when the emulator controller device lives within an IsolatedDevMgr
    // component inside the ScopedInstance.
    async fn create_from_realm(
        realm: &ScopedInstance,
    ) -> Result<(TestDevice, HciEmulatorProxy), Error> {
        let dev_dir = fuchsia_fs::directory::open_directory(
            realm.get_exposed_dir(),
            "dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await
        .context("failed to open /dev file")?;
        let relative_control_dev_path = CONTROL_DEVICE
            .strip_prefix("/dev/")
            .ok_or(format_err!("unexpected control device path: {}", CONTROL_DEVICE))?;
        // The `/dev` channel may be served by the IsolatedDevMgr before the emulator device is
        // actually available. We use the recursive_wait_and_open_node method to ensure that the
        // emulator device is available before we attempt to open it.
        let controller_channel =
            device_watcher::recursive_wait_and_open_node(&dev_dir, relative_control_dev_path)
                .await?
                .into_channel()
                .map_err(|_| format_err!("failed to convert NodeProxy to channel"))?;
        Self::create_internal(controller_channel, Some(dev_dir)).await
    }

    async fn create_internal(
        controller_channel: fasync::Channel,
        dev_directory: Option<fio::DirectoryProxy>,
    ) -> Result<(TestDevice, HciEmulatorProxy), Error> {
        let controller = VirtualControllerProxy::new(controller_channel);
        let name = controller
            .create_emulator()
            .map_err(Error::from)
            .on_timeout(WATCH_TIMEOUT.after_now(), || {
                Err(format_err!("timed out waiting for emulator to create test device"))
            })
            .await?
            .map_err(zx_status::Status::from_raw)?
            .ok_or_else(|| {
                format_err!("name absent from EmulatorController::Create FIDL response")
            })?;

        // Wait until a bt-emulator device gets published under our test device.
        let mut topo_path = PathBuf::from(CONTROL_DEVICE);
        topo_path.push(name);
        let mut watcher =
            dev_watcher_maybe_in_namespace(EMULATOR_DEVICE_DIR, dev_directory.as_ref()).await?;
        let emulator_dev = watcher.watch_new(&topo_path, WatchFilter::AddedOrExisting).await?;

        // Connect to the bt-emulator device.
        let channel = fdio::clone_channel(emulator_dev.file())?;
        let emulator = EmulatorProxy::new(fasync::Channel::from_channel(channel)?);

        // Open a HciEmulator protocol channel.
        let (proxy, remote) = zx::Channel::create()?;
        emulator.open(remote)?;
        let file =
            fdio::create_fd(emulator.into_channel().unwrap().into_zx_channel().into_handle())?;
        Ok((
            TestDevice { file, dev_directory },
            HciEmulatorProxy::new(fasync::Channel::from_channel(proxy)?),
        ))
    }

    /// Sends the test device a destroy message which will unbind the driver.
    /// This will wait for the test device to be unpublished from devfs.
    pub async fn destroy_and_wait(&mut self) -> Result<(), Error> {
        let mut watcher =
            dev_watcher_maybe_in_namespace(CONTROL_DEVICE, self.dev_directory.as_ref()).await?;
        let topo_path = PathBuf::from(fdio::device_get_topo_path(&self.file)?);
        let relative_device_path = topo_path.strip_prefix(CONTROL_DEVICE).map_err(|e| {
            format_err!("device topo path doesn't match expected emulator control path: {:?}", e)
        })?;
        self.destroy().await?;
        watcher.watch_removed(relative_device_path).await
    }

    // Send the test device a destroy message which will unbind the driver.
    async fn destroy(&mut self) -> Result<(), Error> {
        let channel = fdio::clone_channel(&self.file)?;
        let device = ControllerProxy::new(fasync::Channel::from_channel(channel)?);
        Ok(device.schedule_unbind().await?.map_err(zx_status::Status::from_raw)?)
    }
}

// Returns a DeviceWatcher watching `path`, which should start with `/dev`. The watched directory is
//   - The `/dev/`-stripped part of `path` relative to `dev_dir`, if `dev_dir` is present.
//   - `path` within the component's namespace, if `dev_dir` is not present.
async fn dev_watcher_maybe_in_namespace(
    path: &str,
    dev_dir: Option<&fio::DirectoryProxy>,
) -> Result<DeviceWatcher, Error> {
    if let Some(dir) = dev_dir {
        let stripped_path = Path::new(path).strip_prefix("/dev")?.to_string_lossy();
        let open_dir = fuchsia_fs::directory::open_directory(
            dir,
            stripped_path.as_ref(),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await?;
        DeviceWatcher::new(path, open_dir, WATCH_TIMEOUT).await
    } else {
        DeviceWatcher::new_in_namespace(path, WATCH_TIMEOUT).await
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_bluetooth_test::EmulatorError,
        fidl_fuchsia_driver_test as fdt, fuchsia,
        fuchsia_component_test::RealmBuilder,
        fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    };

    fn default_settings() -> EmulatorSettings {
        EmulatorSettings {
            address: None,
            hci_config: None,
            extended_advertising: None,
            acl_buffer_settings: None,
            le_acl_buffer_settings: None,
            ..EmulatorSettings::EMPTY
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/35077) Re-enable once test flake is resolved
    async fn test_publish_lifecycle() {
        // We use these watchers to verify the addition and removal of these devices as tied to the
        // lifetime of the Emulator instance we create below.
        let mut hci_watcher: DeviceWatcher;
        let mut emul_watcher: DeviceWatcher;
        let hci_dev: DeviceFile;
        let emul_dev: DeviceFile;

        {
            let mut fake_dev = Emulator::create(None).await.expect("Failed to construct Emulator");
            let topo_path = fdio::device_get_topo_path(&fake_dev.file())
                .expect("Failed to obtain topological path for Emulator");
            let topo_path = PathBuf::from(topo_path);

            // A bt-emulator device should already exist by now.
            emul_watcher = DeviceWatcher::new_in_namespace(EMULATOR_DEVICE_DIR, WATCH_TIMEOUT)
                .await
                .expect("Failed to create bt-emulator device watcher");
            emul_dev = emul_watcher
                .watch_existing(&topo_path)
                .await
                .expect("Expected bt-emulator device to have been published");

            // Send a publish message to the device. This call should succeed and result in a new
            // bt-hci device. (Note: it is important for `hci_watcher` to get constructed here since
            // our expectation is based on the `ADD_FILE` event).
            hci_watcher = DeviceWatcher::new_in_namespace(HCI_DEVICE_DIR, WATCH_TIMEOUT)
                .await
                .expect("Failed to create bt-hci device watcher");
            let _ = fake_dev
                .publish(default_settings())
                .await
                .expect("Failed to send Publish message to emulator device");
            hci_dev = hci_watcher
                .watch_new(&topo_path, WatchFilter::AddedOnly)
                .await
                .expect("Expected a new bt-hci device");

            // Once a device is published, it should not be possible to publish again while the
            // HciEmulator channel is open.
            let result = fake_dev
                .emulator()
                .publish(default_settings())
                .await
                .expect("Failed to send second Publish message to emulator device");
            assert_eq!(Err(EmulatorError::HciAlreadyPublished), result);

            fake_dev.destroy_and_wait().await.expect("Expected test device to be removed");
        }

        // Both devices should be destroyed when `fake_dev` gets dropped.
        let _ = hci_watcher
            .watch_removed(hci_dev.relative_path())
            .await
            .expect("Expected bt-hci device to get removed");
        let _ = emul_watcher
            .watch_removed(emul_dev.relative_path())
            .await
            .expect("Expected bt-emulator device to get removed");
    }

    #[fuchsia::test]
    async fn publish_lifecycle_with_realm() {
        let realm = RealmBuilder::new().await.unwrap();
        let _ = realm.driver_test_realm_setup().await.unwrap();
        let realm = realm.build().await.expect("failed to build realm");
        let args = fdt::RealmArgs {
            root_driver: Some("fuchsia-boot:///#driver/platform-bus.so".to_string()),
            ..fdt::RealmArgs::EMPTY
        };
        realm.driver_test_realm_start(args).await.unwrap();
        let dev_dir = fuchsia_fs::directory::open_directory(
            realm.root.get_exposed_dir(),
            "dev",
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await
        .unwrap();
        // We use these watchers to verify the addition and removal of these devices as tied to the
        // lifetime of the Emulator instance we create below.
        let mut hci_watcher: DeviceWatcher;
        let mut emul_watcher: DeviceWatcher;
        let hci_dev: DeviceFile;
        let emul_dev: DeviceFile;

        {
            let mut fake_dev =
                Emulator::create(Some(&realm.root)).await.expect("Failed to construct Emulator");
            let topo_path = fdio::device_get_topo_path(&fake_dev.file())
                .expect("Failed to obtain topological path for Emulator");
            let topo_path = PathBuf::from(topo_path);

            // A bt-emulator device should already exist by now.
            emul_watcher = dev_watcher_maybe_in_namespace(EMULATOR_DEVICE_DIR, Some(&dev_dir))
                .await
                .expect("Failed to create bt-emulator device watcher");

            emul_dev = emul_watcher
                .watch_existing(&topo_path)
                .await
                .expect("Expected bt-emulator device to have been published");

            // Send a publish message to the device. This call should succeed and result in a new
            // bt-hci device. (Note: it is important for `hci_watcher` to get constructed here since
            // our expectation is based on the `ADD_FILE` event).
            hci_watcher = dev_watcher_maybe_in_namespace(HCI_DEVICE_DIR, Some(&dev_dir))
                .await
                .expect("Failed to create bt-hci device watcher");

            let _ = fake_dev
                .publish(default_settings())
                .await
                .expect("Failed to send Publish message to emulator device");
            hci_dev = hci_watcher
                .watch_new(&topo_path, WatchFilter::AddedOnly)
                .await
                .expect("Expected a new bt-hci device");

            // Once a device is published, it should not be possible to publish again while the
            // HciEmulator channel is open.
            let result = fake_dev
                .emulator()
                .publish(default_settings())
                .await
                .expect("Failed to send second Publish message to emulator device");
            assert_eq!(Err(EmulatorError::HciAlreadyPublished), result);

            fake_dev.destroy_and_wait().await.expect("Expected test device to be removed");
        }

        // Both devices should be destroyed when `fake_dev` gets dropped.
        let _ = hci_watcher
            .watch_removed(hci_dev.relative_path())
            .await
            .expect("Expected bt-hci device to get removed");
        let _ = emul_watcher
            .watch_removed(emul_dev.relative_path())
            .await
            .expect("Expected bt-emulator device to get removed");
    }
}
