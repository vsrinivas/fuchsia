// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        crypt::insecure::InsecureCrypt,
        filesystem::{FxFilesystem, OpenFxFilesystem},
        fsck::{errors::FsckIssue, fsck_volume_with_options, fsck_with_options, FsckOptions},
        object_store::volume::root_volume,
        platform::fuchsia::volume::FxVolumeAndRoot,
    },
    anyhow::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io as fio,
    fuchsia_zircon::Status,
    std::sync::Arc,
    storage_device::{fake_device::FakeDevice, DeviceHolder},
    vfs::{directory::entry::DirectoryEntry, path::Path},
};

struct State {
    filesystem: OpenFxFilesystem,
    volume: FxVolumeAndRoot,
    root: fio::DirectoryProxy,
}

impl From<State> for (OpenFxFilesystem, FxVolumeAndRoot) {
    fn from(state: State) -> (OpenFxFilesystem, FxVolumeAndRoot) {
        (state.filesystem, state.volume)
    }
}

pub struct TestFixture {
    state: Option<State>,
    encrypted: bool,
}

impl TestFixture {
    pub async fn new() -> Self {
        Self::open(DeviceHolder::new(FakeDevice::new(16384, 512)), true, true).await
    }

    pub async fn new_unencrypted() -> Self {
        Self::open(DeviceHolder::new(FakeDevice::new(16384, 512)), true, false).await
    }

    pub async fn open(device: DeviceHolder, format: bool, encrypted: bool) -> Self {
        let (filesystem, volume) = if format {
            let filesystem = FxFilesystem::new_empty(device).await.unwrap();
            let root_volume = root_volume(filesystem.clone()).await.unwrap();
            let vol = FxVolumeAndRoot::new(
                root_volume
                    .new_volume(
                        "vol",
                        if encrypted { Some(Arc::new(InsecureCrypt::new())) } else { None },
                    )
                    .await
                    .unwrap(),
                0,
            )
            .await
            .unwrap();
            (filesystem, vol)
        } else {
            let filesystem = FxFilesystem::open(device).await.unwrap();
            let root_volume = root_volume(filesystem.clone()).await.unwrap();
            let vol = FxVolumeAndRoot::new(
                root_volume
                    .volume(
                        "vol",
                        if encrypted { Some(Arc::new(InsecureCrypt::new())) } else { None },
                    )
                    .await
                    .unwrap(),
                0,
            )
            .await
            .unwrap();
            (filesystem, vol)
        };
        let (root, server_end) =
            create_proxy::<fio::DirectoryMarker>().expect("create_proxy failed");
        volume.root().clone().open(
            volume.volume().scope().clone(),
            fio::OpenFlags::DIRECTORY
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(server_end.into_channel()),
        );
        Self { state: Some(State { filesystem, volume, root }), encrypted }
    }

    /// Closes the test fixture, shutting down the filesystem. Returns the device, which can be
    /// reused for another TestFixture.
    ///
    /// Ensures that:
    ///   * The filesystem shuts down cleanly.
    ///   * fsck passes.
    ///   * There are no dangling references to the device or the volume.
    pub async fn close(mut self) -> DeviceHolder {
        let state = std::mem::take(&mut self.state).unwrap();
        // Close the root node and ensure that there's no remaining references to |vol|, which would
        // indicate a reference cycle or other leak.
        state
            .root
            .close()
            .await
            .expect("FIDL call failed")
            .map_err(Status::from_raw)
            .expect("close root failed");
        let (filesystem, volume) = state.into();

        let store_id = volume.volume().store().store_object_id();

        // Wait for all tasks to finish running.  If we don't do this, it's possible that we haven't
        // yet noticed that a connection has closed, and so tasks can still be running and they can
        // hold references to the volume which we want to unwrap.
        volume.volume().scope().wait().await;

        volume.volume().terminate().await;

        Arc::try_unwrap(volume.into_volume())
            .map_err(|_| "References to volume still exist")
            .unwrap();

        // We have to reopen the filesystem briefly to fsck it. (We could fsck before closing, but
        // there might be pending operations that go through after fsck but before we close the
        // filesystem, and we want to be sure that we catch all possible issues with fsck.)
        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.ensure_unique();
        device.reopen(false);
        let filesystem = FxFilesystem::open(device).await.expect("open failed");
        let options = FsckOptions {
            fail_on_warning: true,
            on_error: Box::new(|err: &FsckIssue| {
                eprintln!("Fsck error: {:?}", err);
            }),
            ..Default::default()
        };
        fsck_with_options(filesystem.clone(), &options).await.expect("fsck failed");
        fsck_volume_with_options(
            filesystem.as_ref(),
            &options,
            store_id,
            if self.encrypted { Some(Arc::new(InsecureCrypt::new())) } else { None },
        )
        .await
        .expect("fsck_volume failed");

        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.ensure_unique();
        device.reopen(false);

        device
    }

    pub fn root(&self) -> &fio::DirectoryProxy {
        &self.state.as_ref().unwrap().root
    }

    pub fn fs(&self) -> &FxFilesystem {
        &self.state.as_ref().unwrap().filesystem
    }
}

impl Drop for TestFixture {
    fn drop(&mut self) {
        assert!(self.state.is_none(), "Did you forget to call TestFixture::close?");
    }
}

pub async fn close_file_checked(file: fio::FileProxy) {
    file.sync().await.expect("FIDL call failed").map_err(Status::from_raw).expect("sync failed");
    file.close().await.expect("FIDL call failed").map_err(Status::from_raw).expect("close failed");
}

pub async fn close_dir_checked(dir: fio::DirectoryProxy) {
    dir.close().await.expect("FIDL call failed").map_err(Status::from_raw).expect("close failed");
}

// Utility function to open a new node connection under |dir|.
pub async fn open_file(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    mode: u32,
    path: &str,
) -> Result<fio::FileProxy, Error> {
    let (proxy, server_end) = create_proxy::<fio::FileMarker>().expect("create_proxy failed");
    dir.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;
    proxy.describe_deprecated().await?;
    Ok(proxy)
}

// Like |open_file|, but asserts if the open call fails.
pub async fn open_file_checked(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    mode: u32,
    path: &str,
) -> fio::FileProxy {
    open_file(dir, flags, mode, path).await.expect("open_file failed")
}

// Utility function to open a new node connection under |dir|.
pub async fn open_dir(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    mode: u32,
    path: &str,
) -> Result<fio::DirectoryProxy, Error> {
    let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>().expect("create_proxy failed");
    dir.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;
    proxy.describe_deprecated().await?;
    Ok(proxy)
}

// Like |open_dir|, but asserts if the open call fails.
pub async fn open_dir_checked(
    dir: &fio::DirectoryProxy,
    flags: fio::OpenFlags,
    mode: u32,
    path: &str,
) -> fio::DirectoryProxy {
    open_dir(dir, flags, mode, path).await.expect("open_dir failed")
}
