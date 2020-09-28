// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::util::open_rdwr,
    anyhow::{format_err, Error},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_vfs_watcher::{WatchEvent, Watcher as VfsWatcher},
    fuchsia_zircon as zx,
    futures::{Future, TryStreamExt},
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    log::{error, info},
    std::{
        fs::File,
        path::{Path, PathBuf},
    },
};

pub struct DeviceFile {
    /// Open handle to the device file.
    file: File,

    /// The path of the device in the current namespace.
    path: PathBuf,

    /// Topological path of the device.
    topo_path: PathBuf,
}

impl DeviceFile {
    pub fn file(&self) -> &File {
        &self.file
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn topo_path(&self) -> &Path {
        &self.topo_path
    }
}

impl DeviceFile {
    pub fn open(path: &Path) -> Result<DeviceFile, Error> {
        let f = open_rdwr(path)?;
        let topo = fdio::device_get_topo_path(&f)?;
        Ok(DeviceFile { file: f, path: path.to_path_buf(), topo_path: PathBuf::from(topo) })
    }
}

/// Utility object for watching for device creation and removal events.
pub struct DeviceWatcher {
    dir: PathBuf,
    watcher: VfsWatcher,
    timeout: zx::Duration,
}

/// Filter used when watching for new devices.
pub enum WatchFilter {
    /// `DeviceWatcher::watch_new` resolves only for new device additions
    AddedOnly,

    /// `DeviceWatcher::watch_new` resolves for existing and new additions
    AddedOrExisting,
}

impl DeviceWatcher {
    pub async fn new(dir: &str, timeout: zx::Duration) -> Result<DeviceWatcher, Error> {
        let dir_proxy = open_directory_in_namespace(dir, OPEN_RIGHT_READABLE)?;
        Ok(DeviceWatcher {
            dir: PathBuf::from(dir),
            watcher: VfsWatcher::new(dir_proxy).await?,
            timeout,
        })
    }

    /// Functions for watching devices. All of these return a Future that resolves when the desired
    /// condition is met. The Future resolves in an error if the condition is not met within the
    /// `timeout` interval that the DeviceWatcher was constructed with.

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
            WatchFilter::AddedOrExisting => vec![WatchEvent::ADD_FILE, WatchEvent::EXISTING],
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

    /// Wait until a device with the given `path` gets removed. The Future will time out if a
    /// timeout has been set via set_timeout().
    pub fn watch_removed<'a>(
        &'a mut self,
        path: &'a Path,
    ) -> impl Future<Output = Result<(), Error>> + 'a {
        let path_str = path.display();
        let t = self.timeout;
        self.removed_helper(path).on_timeout(t.after_now(), move || {
            Err(format_err!("timed out waiting for device {}", path_str))
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
            if events.contains(&msg.event) {
                let path = self.dir.join(msg.filename);
                let dev = match DeviceFile::open(&path) {
                    Ok(d) => d,
                    Err(e) => {
                        error!("Failed to open file (path: {}) {:#?}", path.to_string_lossy(), e);
                        // Ignore failures potentially triggered by devices we're not interested in.
                        continue;
                    }
                };
                if dev.topo_path().starts_with(topo_path) {
                    info!("found device: {:#?}", dev.path());
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
        let path_str = topo_path.display();
        self.watch_helper(topo_path, events).on_timeout(t.after_now(), move || {
            Err(format_err!("timed out waiting for device {}", path_str))
        })
    }

    // Helper for watching for removal.
    async fn removed_helper<'a>(&'a mut self, path: &'a Path) -> Result<(), Error> {
        while let Some(msg) = self.watcher.try_next().await? {
            match msg.event {
                WatchEvent::REMOVE_FILE => {
                    if self.dir.join(msg.filename) == path {
                        return Ok(());
                    }
                }
                _ => (),
            }
        }
        unreachable!();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_device_test::{
        DeviceSynchronousProxy, RootDeviceSynchronousProxy, CONTROL_DEVICE,
    };

    const TIMEOUT: zx::Duration = zx::Duration::from_seconds(10);

    fn create_test_dev(name: &str) -> Result<DeviceFile, Error> {
        let control = open_rdwr(CONTROL_DEVICE)?;
        let mut root_dev = RootDeviceSynchronousProxy::new(fdio::clone_channel(&control)?);

        let (local, remote) = zx::Channel::create()?;
        let (status, path) =
            root_dev.create_device(name, Some(remote), zx::Time::after(TIMEOUT))?;
        zx::Status::ok(status)?;

        let path =
            PathBuf::from(path.ok_or(format_err!("RootDevice.CreateDevice returned null path"))?);
        let file = fdio::create_fd(zx::Handle::from(local))?;
        let topo_path = PathBuf::from(fdio::device_get_topo_path(&file)?);
        Ok(DeviceFile { file, path, topo_path })
    }

    fn remove_test_dev(dev: &DeviceFile) -> Result<(), Error> {
        let channel = fdio::clone_channel(dev.file())?;
        let mut device = DeviceSynchronousProxy::new(channel);
        Ok(device.destroy()?)
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/35077) Re-enable once test flake is resolved
    async fn test_watch_new() {
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");
        let dev = create_test_dev("test-watch-new").expect("Failed to create test device");
        error!("created: {:?}", dev.topo_path());
        let found = watcher
            .watch_new(dev.topo_path(), WatchFilter::AddedOnly)
            .await
            .expect("Expected to be notified of new test device");
        assert_eq!(dev.path(), found.path());
        assert_eq!(dev.topo_path(), found.topo_path());

        // Calling with the `existing` flag should succeed.
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");
        let found = watcher
            .watch_new(dev.topo_path(), WatchFilter::AddedOrExisting)
            .await
            .expect("Expected to be notified of existing test device");
        assert_eq!(dev.path(), found.path());
        assert_eq!(dev.topo_path(), found.topo_path());

        // Cleanup after ourselves
        remove_test_dev(&dev).expect("Failed to remove test device");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/35077) Re-enable once test flake is resolved
    async fn test_watch_existing() {
        let dev = create_test_dev("test-watch-existing").expect("Failed to create test device");
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");
        let found = watcher
            .watch_existing(dev.topo_path())
            .await
            .expect("Expected to be notified of new test device");
        assert_eq!(dev.path(), found.path());
        assert_eq!(dev.topo_path(), found.topo_path());

        // Cleanup after ourselves
        remove_test_dev(&dev).expect("Failed to remove test device");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/35077) Re-enable once test flake is resolved
    async fn test_watch_removed() {
        let dev = create_test_dev("test-watch-removed").expect("Failed to create test device");
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, TIMEOUT)
            .await
            .expect("Failed to create watcher for test devices");

        remove_test_dev(&dev).expect("Failed to remove test device");
        let _ = watcher
            .watch_removed(dev.path())
            .await
            .expect("Expected to be notified of device removal");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    #[ignore] // TODO(fxbug.dev/35077) Re-enable once test flake is resolved
    async fn test_watch_timeout() {
        let mut watcher = DeviceWatcher::new(CONTROL_DEVICE, zx::Duration::from_nanos(0))
            .await
            .expect("Failed to create watcher");
        let path = PathBuf::from("/device_watcher/test_watch_timeout");

        let result = watcher.watch_new(&path, WatchFilter::AddedOnly).await;
        assert!(!result.is_ok());
        let result = watcher.watch_new(&path, WatchFilter::AddedOrExisting).await;
        assert!(!result.is_ok());
        let result = watcher.watch_existing(&path).await;
        assert!(!result.is_ok());
        let result = watcher.watch_removed(&path).await;
        assert!(!result.is_ok());
    }
}
