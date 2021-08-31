// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        object_store::{
            crypt::InsecureCrypt,
            filesystem::{FxFilesystem, OpenFxFilesystem},
            fsck::fsck,
            volume::{create_root_volume, root_volume},
        },
        server::volume::{FxVolume, FxVolumeAndRoot},
    },
    anyhow::Error,
    fidl::endpoints::{create_proxy, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FileMarker, FileProxy, MODE_TYPE_DIRECTORY,
        OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    std::sync::Arc,
    storage_device::{fake_device::FakeDevice, DeviceHolder},
    vfs::{
        directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path,
        registry::token_registry,
    },
};

struct State {
    filesystem: OpenFxFilesystem,
    volume: FxVolumeAndRoot,
    root: DirectoryProxy,
}

impl From<State> for (OpenFxFilesystem, FxVolumeAndRoot) {
    fn from(state: State) -> (OpenFxFilesystem, FxVolumeAndRoot) {
        (state.filesystem, state.volume)
    }
}

pub struct TestFixture {
    scope: ExecutionScope,
    state: Option<State>,
}

impl TestFixture {
    pub async fn new() -> Self {
        Self::open(DeviceHolder::new(FakeDevice::new(8192, 512)), true).await
    }

    pub async fn open(device: DeviceHolder, format: bool) -> Self {
        let (filesystem, volume) = if format {
            let filesystem =
                FxFilesystem::new_empty(device, Arc::new(InsecureCrypt::new())).await.unwrap();
            let root_volume = create_root_volume(&filesystem).await.unwrap();
            let vol =
                FxVolumeAndRoot::new(root_volume.new_volume("vol").await.unwrap()).await.unwrap();
            (filesystem, vol)
        } else {
            let filesystem =
                FxFilesystem::open(device, Arc::new(InsecureCrypt::new())).await.unwrap();
            let root_volume =
                root_volume(&filesystem).await.unwrap().expect("root-volume not found");
            let vol = FxVolumeAndRoot::new(root_volume.volume("vol").await.unwrap()).await.unwrap();
            (filesystem, vol)
        };
        let scope = ExecutionScope::build().token_registry(token_registry::Simple::new()).new();
        let (root, server_end) = create_proxy::<DirectoryMarker>().expect("create_proxy failed");
        volume.root().clone().open(
            scope.clone(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(server_end.into_channel()),
        );
        Self { scope, state: Some(State { filesystem, volume, root }) }
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
        Status::ok(state.root.close().await.expect("FIDL call failed")).expect("close root failed");
        let (filesystem, volume) = state.into();

        // Wait for all tasks to finish running.  If we don't do this, it's possible that we haven't
        // yet noticed that a connection has closed, and so tasks can still be running and they can
        // hold references to the volume which we want to unwrap.
        self.scope.wait().await;

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
        device.reopen();
        let filesystem =
            FxFilesystem::open(device, Arc::new(InsecureCrypt::new())).await.expect("open failed");
        fsck(&filesystem).await.expect("fsck failed");

        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.ensure_unique();
        device.reopen();

        self.scope.shutdown();
        device
    }

    pub fn root(&self) -> &DirectoryProxy {
        &self.state.as_ref().unwrap().root
    }

    pub fn fs(&self) -> &FxFilesystem {
        &self.state.as_ref().unwrap().filesystem
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.state.as_ref().unwrap().volume.volume()
    }
}

impl Drop for TestFixture {
    fn drop(&mut self) {
        assert!(self.state.is_none(), "Did you forget to call TestFixture::close?");
    }
}

pub async fn close_file_checked(file: FileProxy) {
    Status::ok(file.sync().await.expect("FIDL call failed")).expect("sync failed");
    Status::ok(file.close().await.expect("FIDL call failed")).expect("close failed");
}

pub async fn close_dir_checked(dir: DirectoryProxy) {
    Status::ok(dir.close().await.expect("FIDL call failed")).expect("close failed");
}

// Utility function to open a new node connection under |dir|.
pub async fn open_file(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<FileProxy, Error> {
    let (proxy, server_end) = create_proxy::<FileMarker>().expect("create_proxy failed");
    dir.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;
    proxy.describe().await?;
    Ok(proxy)
}

// Like |open_file|, but asserts if the open call fails.
pub async fn open_file_checked(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> FileProxy {
    open_file(dir, flags, mode, path).await.expect("open_file failed")
}

// Utility function to open a new node connection under |dir|.
pub async fn open_dir(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> Result<DirectoryProxy, Error> {
    let (proxy, server_end) = create_proxy::<DirectoryMarker>().expect("create_proxy failed");
    dir.open(flags, mode, path, ServerEnd::new(server_end.into_channel()))?;
    proxy.describe().await?;
    Ok(proxy)
}

// Like |open_dir|, but asserts if the open call fails.
pub async fn open_dir_checked(
    dir: &DirectoryProxy,
    flags: u32,
    mode: u32,
    path: &str,
) -> DirectoryProxy {
    open_dir(dir, flags, mode, path).await.expect("open_dir failed")
}
