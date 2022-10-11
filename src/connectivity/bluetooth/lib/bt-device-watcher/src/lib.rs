// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Convenience library that wraps around the VFS watcher.

use {
    anyhow::{format_err, Error},
    fidl::{endpoints::Proxy, HandleBased},
    fidl_fuchsia_io as fio,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_vfs_watcher::{WatchEvent, Watcher as VfsWatcher},
    fuchsia_zircon as zx,
    futures::{Future, TryStreamExt},
    std::{
        fs::File,
        path::{Path, PathBuf},
    },
    tracing::{error, info},
};

/// A representation of an open device file, as constructed by a DeviceWatcher. A DeviceFile is
/// necessarily linked to the directory of the DeviceWatcher that opened it, as the `relative_path`
/// method is relative to the directory watched by that DeviceWatcher.
#[derive(Debug)]
pub struct DeviceFile {
    /// Open handle to the device file.
    file: File,

    /// The path of the device relative to the directory of the DeviceWatcher that created it.
    relative_path: PathBuf,

    /// Topological path of the device.
    topo_path: PathBuf,
}

impl DeviceFile {
    pub fn file(&self) -> &File {
        &self.file
    }

    pub fn relative_path(&self) -> &Path {
        &self.relative_path
    }

    pub fn topo_path(&self) -> &Path {
        &self.topo_path
    }
}

/// Utility object for watching for device creation and removal events.
pub struct DeviceWatcher {
    debug_dir_name: PathBuf,
    watcher: VfsWatcher,
    timeout: zx::Duration,
    watched_dir: fio::DirectoryProxy,
}

/// Filter used when watching for new devices.
pub enum WatchFilter {
    /// `DeviceWatcher::watch_new` resolves only for new device additions
    AddedOnly,

    /// `DeviceWatcher::watch_new` resolves for existing and new additions
    AddedOrExisting,
}

impl DeviceWatcher {
    /// Returns a DeviceWatcher. `open_dir` is an existing proxy to the directory to be watched.
    /// `debug_dir_path` is the path to `open_dir`, passed along for clearer debugging.
    ///
    /// All `watch_*` operations return a Future that resolves successfully when the condition is
    /// met, or in an error if the condition is not met within `timeout`.
    pub async fn new(
        debug_dir_name: &str,
        open_dir: fio::DirectoryProxy,
        timeout: zx::Duration,
    ) -> Result<DeviceWatcher, Error> {
        let watched_dir = Clone::clone(&open_dir);
        Ok(DeviceWatcher {
            debug_dir_name: PathBuf::from(debug_dir_name),
            watcher: VfsWatcher::new(open_dir).await?,
            timeout,
            watched_dir,
        })
    }

    /// Like `new`, but instead of an already-open directory, `dir` is the path in the component
    /// namespace of the directory to be watched.
    pub async fn new_in_namespace(
        dir: &str,
        timeout: zx::Duration,
    ) -> Result<DeviceWatcher, Error> {
        let open_dir =
            fuchsia_fs::directory::open_in_namespace(dir, fio::OpenFlags::RIGHT_READABLE)?;
        Self::new(dir, open_dir, timeout).await
    }

    /// Functions for watching devices based on topological path. The topological path is an
    /// absolute path to a directory. This means it includes the entire topology up to the root
    /// device, and that these methods returns once ANY new device is created under that directory.
    //
    // TODO(fxbug.dev/85719): Consider returning custom error type from watch_* methods to more
    // specifically identify failure causes, instead of just anyhow::Error.

    /// Wait until a new device is added under `topo_path`. If `existing` is false, then the Future is
    /// satisfied only if the file is created after the creation of this DeviceWatcher or since the
    /// last watch event related to this file.
    pub fn watch_new<'a>(
        &'a mut self,
        topo_path: &'a Path,
        filter: WatchFilter,
    ) -> impl Future<Output = Result<DeviceFile, Error>> + 'a {
        let events = match filter {
            WatchFilter::AddedOnly => vec![WatchEvent::ADD_FILE],
            WatchFilter::AddedOrExisting => {
                vec![WatchEvent::ADD_FILE, WatchEvent::EXISTING]
            }
        };
        self.watch_with_timeout(topo_path, events)
    }

    /// Similar to `watch_new` but returns a Future that is satisifed only if a file already existed
    /// at the creation of this DeviceWatcher.
    pub fn watch_existing<'a>(
        &'a mut self,
        topo_path: &'a Path,
    ) -> impl Future<Output = Result<DeviceFile, Error>> + 'a {
        self.watch_with_timeout(topo_path, vec![WatchEvent::EXISTING])
    }

    /// Wait until a device with the given `relative_path` gets removed.
    /// NOTE: While watch_new and watch_existing open all files they are notified of and wait for
    /// a device under the provided toplogical path, this is different. It takes the file name of
    /// a device in the current directory, and matches removal notifications directly against that
    /// path. As such, it rejects paths with multiple components, as the DeviceWatcher can only
    /// watch for changes to its immediate children.
    pub fn watch_removed<'a>(
        &'a mut self,
        entry_path: &'a Path,
    ) -> impl Future<Output = Result<(), Error>> + 'a {
        let debug_path = self.debug_dir_name.join(entry_path);
        let t = self.timeout;
        self.removed_helper(entry_path).on_timeout(t.after_now(), move || {
            Err(format_err!("timed out waiting for device {:?}", debug_path))
        })
    }

    // Private functions:

    // Helper for watching new or existing files. It is incorrect for `events` to contain
    // `WatchEvent::REMOVE_FILE` as it is not possible to open a removed file and check its
    // topological path.
    async fn watch_helper<'a>(
        &'a mut self,
        topo_path: &'a Path,
        events: Vec<WatchEvent>,
    ) -> Result<DeviceFile, Error> {
        assert!(!events.contains(&WatchEvent::REMOVE_FILE));
        while let Some(msg) = self.watcher.try_next().await? {
            if msg.filename == Path::new(".") {
                continue;
            }
            if events.contains(&msg.event) {
                let relative_path = msg.filename.as_path();
                let debug_path = self.debug_dir_name.join(relative_path);
                let opened_dev = self.open_device_file(relative_path.to_owned());
                let dev = match opened_dev {
                    Ok(d) => d,
                    Err(e) => {
                        error!("Failed to open file (path: {:?}) {:#?}", debug_path, e);
                        // Ignore failures potentially triggered by devices we're not interested in.
                        continue;
                    }
                };
                if dev.topo_path().starts_with(topo_path) {
                    info!("found device: {:#?}", debug_path);
                    return Ok(dev);
                }
            }
        }
        unreachable!();
    }

    // Helper that wraps `watch_helper` in a timeout.
    fn watch_with_timeout<'a>(
        &'a mut self,
        topo_path: &'a Path,
        events: Vec<WatchEvent>,
    ) -> impl Future<Output = Result<DeviceFile, Error>> + 'a {
        let t = self.timeout;
        self.watch_helper(topo_path, events).on_timeout(t.after_now(), move || {
            Err(format_err!("timed out waiting for device {:?}", topo_path))
        })
    }

    // Helper for watching for removal.
    async fn removed_helper<'a>(&'a mut self, entry_path: &'a Path) -> Result<(), Error> {
        if !entry_path.is_relative() || entry_path.components().count() != 1 {
            return Err(format_err!(
                "path to entry {:?} must be relative and have only one component",
                entry_path
            ));
        }
        while let Some(msg) = self.watcher.try_next().await? {
            match msg.event {
                WatchEvent::REMOVE_FILE => {
                    if msg.filename == entry_path {
                        return Ok(());
                    }
                }
                _ => (),
            }
        }
        unreachable!();
    }

    fn open_device_file(&self, relative_path: PathBuf) -> Result<DeviceFile, Error> {
        let file = fuchsia_fs::open_file(
            &self.watched_dir,
            relative_path.as_path(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )?;
        let file = fdio::create_fd(
            file.into_channel()
                .map_err(|_| format_err!("failed to convert FileProxy to channel"))?
                .into_zx_channel()
                .into_handle(),
        )?;
        let topo = fdio::device_get_topo_path(&file)?;
        Ok(DeviceFile { file, relative_path, topo_path: PathBuf::from(topo) })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        assert_matches::assert_matches,
        fidl_fuchsia_device_test::{
            DeviceSynchronousProxy, RootDeviceSynchronousProxy, CONTROL_DEVICE,
        },
        fidl_fuchsia_driver_test as fdt,
        fuchsia_component_test::{RealmBuilder, RealmInstance},
        fuchsia_driver_test::{DriverTestRealmBuilder, DriverTestRealmInstance},
    };

    const TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);
    // The control device path from fuchsia.device.test.CONTROL_DEVICE is `/dev/sys/test/test`,
    // but because we use the relative path so frequently, we define this a separate constant.
    const CONTROL_DEVICE_RELATIVE: &'static str = "sys/test/test";

    /// Used to keep track of return values.
    struct IsolatedDevMgrTest {
        // The realm holding the IsolatedDevMgr
        _realm: RealmInstance,
        // The root `/dev` directory.
        dev_dir: fio::DirectoryProxy,
        // The test root controller, generally found at `/dev/sys/test/test`
        control_dev_dir: fio::DirectoryProxy,
    }

    /// On success, returns the RealmInstance containing an IsolatedDevMgr, alongside the DevMgrs's
    /// `/dev` dir and the test control device directory. The RealmInstance must not be dropped
    /// while the DevMgr is in use.
    async fn create_isolated_devmgr() -> Result<IsolatedDevMgrTest, Error> {
        let realm = RealmBuilder::new().await.unwrap();
        let _ = realm.driver_test_realm_setup().await?;
        let realm = realm.build().await.expect("failed to build realm");
        let _ = realm.driver_test_realm_start(fdt::RealmArgs::EMPTY).await?;
        let dev_dir = fuchsia_fs::directory::open_directory(
            realm.root.get_exposed_dir(),
            "dev",
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )
        .await?;
        // Block until the test control device is ready.
        let _control_dev_node =
            device_watcher::recursive_wait_and_open_node(&dev_dir, CONTROL_DEVICE_RELATIVE).await?;
        // One might think that it would be safe to just directly convert the control_dev_node to
        // a DirectoryProxy and return that. However, DeviceWatchers created from the converted
        // NodeProxy don't work - likely because we also open a channel to the control device as a
        // RootDeviceProxy later in the test.
        let control_dev_dir = fuchsia_fs::directory::open_directory(
            &dev_dir,
            CONTROL_DEVICE_RELATIVE,
            fio::OpenFlags::RIGHT_READABLE,
        )
        .await?;
        Ok(IsolatedDevMgrTest { _realm: realm, dev_dir, control_dev_dir })
    }

    // Creates a fuchsia.device.test.Device under the root test device. `dev_dir` is the `/dev`
    // directory of an IsolatedDevMgr.
    fn create_test_dev_in_devmgr(
        name: &str,
        dev_dir: &fio::DirectoryProxy,
    ) -> Result<DeviceFile, Error> {
        // Open the control device as a file, then convert the channel to a RootDeviceProxy
        let control_dev_file = fuchsia_fs::open_file(
            dev_dir,
            Path::new(CONTROL_DEVICE_RELATIVE),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
        )?;
        let control_dev_chan = control_dev_file
            .into_channel()
            .expect("failed to convert to channel")
            .into_zx_channel();
        let root_dev = RootDeviceSynchronousProxy::new(control_dev_chan);

        // Create a test device under the `/dev/sys/test/test` root device.
        let (local, remote) = zx::Channel::create()?;
        let (status, path) =
            root_dev.create_device(name, Some(remote), zx::Time::after(TIMEOUT))?;
        zx::Status::ok(status)?;
        let relative_path =
            Path::new(&path.ok_or(format_err!("RootDevice.CreateDevice returned null path"))?)
                .strip_prefix(CONTROL_DEVICE)?
                .to_owned();
        let file = fdio::create_fd(zx::Handle::from(local))?;
        let topo_path = PathBuf::from(fdio::device_get_topo_path(&file)?);
        Ok(DeviceFile { file, relative_path, topo_path })
    }

    fn remove_test_dev(dev: &DeviceFile) -> Result<(), Error> {
        let channel = fdio::clone_channel(dev.file())?;
        let device = DeviceSynchronousProxy::new(channel);
        Ok(device.destroy()?)
    }

    #[fuchsia::test]
    async fn test_watch_new() {
        let IsolatedDevMgrTest { _realm, dev_dir, control_dev_dir } =
            create_isolated_devmgr().await.expect("Failed to create IsolatedDevMgr");
        let control_dev_dir_clone = Clone::clone(&control_dev_dir);
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, control_dev_dir_clone, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");

        let dev = create_test_dev_in_devmgr("test-watch-new", &dev_dir)
            .expect("Failed to create test device");
        let found = watcher
            .watch_new(dev.topo_path(), WatchFilter::AddedOnly)
            .await
            .expect("Expected to be notified of new test device");
        assert_eq!(dev.relative_path(), found.relative_path());
        assert_eq!(dev.topo_path(), found.topo_path());

        // Calling with the `existing` flag should succeed.
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, control_dev_dir, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");
        let found = watcher
            .watch_new(dev.topo_path(), WatchFilter::AddedOrExisting)
            .await
            .expect("Expected to be notified of existing test device");
        assert_eq!(dev.relative_path(), found.relative_path());
        assert_eq!(dev.topo_path(), found.topo_path());

        // Cleanup after ourselves
        remove_test_dev(&dev).expect("Failed to remove test device");
    }

    #[fuchsia::test]
    async fn test_watch_existing() {
        let IsolatedDevMgrTest { _realm, dev_dir, control_dev_dir } =
            create_isolated_devmgr().await.expect("Failed to create IsolatedDevMgr");
        let dev = create_test_dev_in_devmgr("test-watch-existing", &dev_dir)
            .expect("Failed to create test device");
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, control_dev_dir, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");
        let found = watcher
            .watch_existing(dev.topo_path())
            .await
            .expect("Expected to be notified of new test device");
        assert_eq!(dev.relative_path(), found.relative_path());
        assert_eq!(dev.topo_path(), found.topo_path());

        // Cleanup after ourselves
        remove_test_dev(&dev).expect("Failed to remove test device");
    }

    #[fuchsia::test]
    async fn test_watch_removed() {
        let IsolatedDevMgrTest { _realm, dev_dir, control_dev_dir } =
            create_isolated_devmgr().await.expect("Failed to create IsolatedDevMgr");
        let dev = create_test_dev_in_devmgr("test-watch-removed", &dev_dir)
            .expect("Failed to create test device");
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, control_dev_dir, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");

        remove_test_dev(&dev).expect("Failed to remove test device");
        let _ = watcher
            .watch_removed(dev.relative_path())
            .await
            .expect("Expected to be notified of device removal");
    }

    #[fuchsia::test]
    async fn test_watch_timeout() {
        let IsolatedDevMgrTest { _realm, dev_dir: _dev_dir, control_dev_dir } =
            create_isolated_devmgr().await.expect("Failed to create IsolatedDevMgr");
        let mut watcher =
            DeviceWatcher::new(CONTROL_DEVICE, control_dev_dir, zx::Duration::from_nanos(0))
                .await
                .expect("Failed to create watcher");
        let path = PathBuf::from("/device_watcher/test_watch_timeout");

        let result = watcher.watch_new(&path, WatchFilter::AddedOnly).await;
        assert_matches!(result, Err(_));
        let result = watcher.watch_new(&path, WatchFilter::AddedOrExisting).await;
        assert_matches!(result, Err(_));
        let result = watcher.watch_existing(&path).await;
        assert_matches!(result, Err(_));
        let result = watcher.watch_removed(Path::new("relative_path")).await;
        assert_matches!(result, Err(_));
    }

    #[fuchsia::test]
    async fn test_removed_helper_path_validation() {
        let IsolatedDevMgrTest { _realm, dev_dir: _dev_dir, control_dev_dir } =
            create_isolated_devmgr().await.expect("Failed to create IsolatedDevMgr");
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, control_dev_dir, TIMEOUT)
            .await
            .expect("Failed to create watcher");
        let absolute_path = PathBuf::from("/");
        let multiple_component_path = PathBuf::from("device_watcher/test_multiple_components");

        // By using a validation timeout that is smaller than the DeviceWatcher timeout, we ensure
        // that the `watch_removed` failures stem from path validation issues, not timeouts.
        let validation_timeout = TIMEOUT / 5;
        let result = watcher
            .watch_removed(&absolute_path)
            .on_timeout(validation_timeout.after_now(), || {
                panic!("path validation should complete before timeout")
            })
            .await;
        assert_matches!(result, Err(_));
        let result = watcher
            .watch_removed(&multiple_component_path)
            .on_timeout(validation_timeout.after_now(), || {
                panic!("path validation should complete before timeout")
            })
            .await;
        assert_matches!(result, Err(_));
    }
}
