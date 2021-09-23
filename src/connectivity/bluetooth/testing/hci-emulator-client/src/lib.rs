// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    bt_device_watcher::{DeviceFile, DeviceWatcher, WatchFilter},
    fdio,
    fidl::endpoints::Proxy,
    fidl_fuchsia_bluetooth_test::{EmulatorSettings, HciEmulatorProxy},
    fidl_fuchsia_device::ControllerProxy,
    fidl_fuchsia_hardware_bluetooth::{EmulatorControllerProxy, EmulatorProxy},
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_bluetooth::{constants::HOST_DEVICE_DIR, util::open_rdwr},
    fuchsia_zircon::{self as zx, DurationNum, HandleBased},
    fuchsia_zircon_status as zx_status,
    futures::TryFutureExt,
    log::error,
    std::{fs::File, path::PathBuf},
};

fn watch_timeout() -> zx::Duration {
    zx::Duration::from_seconds(10)
}

pub mod types;

// 0x30 => fuchsia.platform.BIND_PLATFORM_DEV_DID.BT_HCI_EMULATOR
pub const CONTROL_DEVICE: &str = "/dev/sys/platform/00:00:30/bt_hci_emulator";
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
    /// `publish()`
    pub async fn create() -> Result<Emulator, Error> {
        let (dev, emulator) =
            TestDevice::create().await.context(format!("Error creating test device"))?;
        Ok(Emulator { dev: Some(dev), emulator: emulator })
    }

    /// Publish a bt-emulator and a bt-hci device using the default emulator settings.
    pub async fn create_and_publish() -> Result<Emulator, Error> {
        let fake_dev = Emulator::create().await?;
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
        let mut watcher = DeviceWatcher::new(HOST_DEVICE_DIR, watch_timeout()).await?;
        let _ = self.publish(settings).await?;
        let topo = PathBuf::from(fdio::device_get_topo_path(self.file())?);
        watcher.watch_new(&topo, WatchFilter::AddedOrExisting).await
    }

    /// Sends the test device a destroy message which will unbind the driver.
    /// This will wait for the test device to be unpublished from devfs.
    pub async fn destroy_and_wait(&mut self) -> Result<(), Error> {
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, watch_timeout()).await?;
        let topo_path = PathBuf::from(fdio::device_get_topo_path(self.file())?);
        let fake_dev = self.dev.take();
        fake_dev
            .expect("attempted to destroy an already destroyed emulator device")
            .destroy()
            .await?;
        watcher.watch_removed(&topo_path).await
    }

    /// Returns a reference to the underlying file.
    pub fn file(&self) -> &File {
        &self.dev.as_ref().expect("emulator device accessed after it was destroyed!").0
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
struct TestDevice(File);

impl TestDevice {
    // Creates a new device as a child of the emulator controller device and obtain the HciEmulator
    // protocol channel.
    async fn create() -> Result<(TestDevice, HciEmulatorProxy), Error> {
        let control_dev =
            open_rdwr(CONTROL_DEVICE).context(format!("Error opening file {}", CONTROL_DEVICE))?;

        let channel = zx::Channel::from(fdio::transfer_fd(control_dev)?);
        let controller = EmulatorControllerProxy::new(fasync::Channel::from_channel(channel)?);

        let name = controller
            .create()
            .map_err(Error::from)
            .on_timeout(10.seconds().after_now(), || {
                Err(format_err!("timed out waiting for emulator to create test device"))
            })
            .await?
            .map_err(zx_status::Status::from_raw)?;

        // Wait until a bt-emulator device gets published under our test device.
        let mut topo_path = PathBuf::from(CONTROL_DEVICE);
        topo_path.push(name.unwrap());
        let mut watcher = DeviceWatcher::new(EMULATOR_DEVICE_DIR, watch_timeout()).await?;
        let emulator_dev = watcher.watch_new(&topo_path, WatchFilter::AddedOrExisting).await?;

        // Connect to the bt-emulator device.
        let channel = fdio::clone_channel(emulator_dev.file())?;
        let emulator = EmulatorProxy::new(fasync::Channel::from_channel(channel)?);

        // Open a HciEmulator protocol channel.
        let (proxy, remote) = zx::Channel::create()?;
        emulator.open(remote)?;
        Ok((
            TestDevice(fdio::create_fd(
                emulator.into_channel().unwrap().into_zx_channel().into_handle(),
            )?),
            HciEmulatorProxy::new(fasync::Channel::from_channel(proxy)?),
        ))
    }

    // Send the test device a destroy message which will unbind the driver.
    async fn destroy(&mut self) -> Result<(), Error> {
        let channel = fdio::clone_channel(&self.0)?;
        let device = ControllerProxy::new(fasync::Channel::from_channel(channel)?);
        Ok(device.schedule_unbind().await?.map_err(zx_status::Status::from_raw)?)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl_fuchsia_bluetooth_test::EmulatorError};

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
            let mut fake_dev = Emulator::create().await.expect("Failed to construct Emulator");
            let topo_path = fdio::device_get_topo_path(&fake_dev.file())
                .expect("Failed to obtain topological path for Emulator");
            let topo_path = PathBuf::from(topo_path);

            // A bt-emulator device should already exist by now.
            emul_watcher = DeviceWatcher::new(EMULATOR_DEVICE_DIR, watch_timeout())
                .await
                .expect("Failed to create bt-emulator device watcher");
            emul_dev = emul_watcher
                .watch_existing(&topo_path)
                .await
                .expect("Expected bt-emulator device to have been published");

            // Send a publish message to the device. This call should succeed and result in a new
            // bt-hci device. (Note: it is important for `hci_watcher` to get constructed here since
            // our expectation is based on the `ADD_FILE` event).
            hci_watcher = DeviceWatcher::new(HCI_DEVICE_DIR, watch_timeout())
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
            .watch_removed(hci_dev.path())
            .await
            .expect("Expected bt-hci device to get removed");
        let _ = emul_watcher
            .watch_removed(emul_dev.path())
            .await
            .expect("Expected bt-emulator device to get removed");
    }
}
