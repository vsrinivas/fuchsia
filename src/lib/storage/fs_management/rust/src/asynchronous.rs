// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Contains the asynchronous version of [`Filesystem`][`crate::Filesystem`].

use {
    crate::{
        error::{BindError, CommandError, KillError, QueryError, ServeError, ShutdownError},
        launch_process, FSConfig,
    },
    cstr::cstr,
    fdio::SpawnAction,
    fidl::endpoints::{ClientEnd, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, FilesystemInfo, NodeMarker, NodeProxy,
        CLONE_FLAG_SAME_RIGHTS,
    },
    fuchsia_async::OnSignals,
    fuchsia_component::client::connect_to_protocol_at_dir_svc,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{Channel, Handle, HandleBased, Process, Signals, Status, Task},
    log::warn,
};

/// Asynchronously manages a block device for filesystem operations.
pub struct Filesystem<FSC> {
    config: FSC,
    block_device: NodeProxy,
}

impl<FSC: FSConfig> Filesystem<FSC> {
    /// Creates a new `Filesystem` with the block device represented by `node_proxy`.
    pub fn from_node(node_proxy: NodeProxy, config: FSC) -> Self {
        Self { config, block_device: node_proxy }
    }

    /// Creates a new `Filesystem` with the block device represented by `channel`.
    pub fn from_channel(channel: Channel, config: FSC) -> Result<Self, fidl::Error> {
        Ok(Self::from_node(ClientEnd::<NodeMarker>::new(channel).into_proxy()?, config))
    }

    // Clone a Channel to the block device.
    fn get_block_handle(&self) -> Result<Handle, fidl::Error> {
        let (block_device, server) = Channel::create().map_err(fidl::Error::ChannelPairCreate)?;
        self.block_device.clone(CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server))?;
        Ok(block_device.into())
    }

    /// Runs `mkfs`, which formats the filesystem onto the block device.
    ///
    /// Which flags are passed to the `mkfs` command are controlled by the config this `Filesystem`
    /// was created with.
    ///
    /// See [`FSConfig`].
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process failed to launch or returned a non-zero exit code.
    pub async fn format(&self) -> Result<(), CommandError> {
        // SpawnAction is not Send, so make sure it is dropped before any `await`s.
        let process = {
            let mut args = vec![self.config.binary_path(), cstr!("mkfs")];
            args.append(&mut self.config.generic_args());
            args.append(&mut self.config.format_args());
            let actions = vec![
                // device handle is passed in as a PA_USER0 handle at argument 1
                SpawnAction::add_handle(
                    HandleInfo::new(HandleType::User0, 1),
                    self.get_block_handle()?,
                ),
            ];
            launch_process(&args, actions)?
        };
        wait_for_successful_exit(process).await
    }

    /// Runs `fsck`, which checks and optionally repairs the filesystem on the block device.
    ///
    /// Which flags are passed to the `fsck` command are controlled by the config this `Filesystem`
    /// was created with.
    ///
    /// See [`FSConfig`].
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process failed to launch or returned a non-zero exit code.
    pub async fn fsck(&self) -> Result<(), CommandError> {
        // SpawnAction is not Send, so make sure it is dropped before any `await`s.
        let process = {
            let mut args = vec![self.config.binary_path(), cstr!("fsck")];
            args.append(&mut self.config.generic_args());
            let actions = vec![
                // device handle is passed in as a PA_USER0 handle at argument 1
                SpawnAction::add_handle(
                    HandleInfo::new(HandleType::User0, 1),
                    self.get_block_handle()?,
                ),
            ];
            launch_process(&args, actions)?
        };
        wait_for_successful_exit(process).await
    }

    /// Serves the filesystem on the block device and returns a [`ServingFilesystem`] representing
    /// the running filesystem process.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if serving the filesystem failed. Use the method
    /// [`ServeErrorAndFilesystem::into_filesystem()`] to recover this `Filesystem` object.
    pub async fn serve(self) -> Result<ServingFilesystem<FSC>, ServeErrorAndFilesystem<FSC>> {
        match self.do_serve().await {
            Ok((process, export_root, root_dir)) => {
                Ok(ServingFilesystem { filesystem: Some(self), process, export_root, root_dir })
            }
            Err(serve_error) => Err(ServeErrorAndFilesystem { serve_error, filesystem: self }),
        }
    }

    async fn do_serve(&self) -> Result<(Process, DirectoryProxy, DirectoryProxy), ServeError> {
        let (export_root, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;

        // SpawnAction is not Send, so make sure it is dropped before any `await`s.
        let process = {
            let mut args = vec![self.config.binary_path(), cstr!("mount")];
            args.append(&mut self.config.generic_args());
            args.append(&mut self.config.mount_args());
            let actions = vec![
                // export root handle is passed in as a PA_DIRECTORY_REQUEST handle at argument 0
                SpawnAction::add_handle(
                    HandleInfo::new(HandleType::DirectoryRequest, 0),
                    server_end.into_channel().into(),
                ),
                // device handle is passed in as a PA_USER0 handle at argument 1
                SpawnAction::add_handle(
                    HandleInfo::new(HandleType::User0, 1),
                    self.get_block_handle()?,
                ),
            ];

            launch_process(&args, actions)?
        };

        // Wait until the filesystem is ready to take incoming requests.
        let (root_dir, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()?;
        export_root.open(
            fidl_fuchsia_io::OPEN_RIGHT_READABLE
                | fidl_fuchsia_io::OPEN_FLAG_POSIX_EXECUTABLE
                | fidl_fuchsia_io::OPEN_FLAG_POSIX_WRITABLE,
            0,
            "root",
            server_end.into_channel().into(),
        )?;
        let _ = root_dir.describe().await?;
        Ok((process, export_root, root_dir))
    }
}

/// Asynchronously manages a serving filesystem. Created from [`Filesystem::serve()`].
pub struct ServingFilesystem<FSC> {
    filesystem: Option<Filesystem<FSC>>,
    process: Process,
    export_root: DirectoryProxy,
    root_dir: DirectoryProxy,
}

impl<FSC: FSConfig> ServingFilesystem<FSC> {
    /// Returns a proxy to the root directory of the serving filesystem.
    pub fn root(&self) -> &DirectoryProxy {
        &self.root_dir
    }

    /// Binds the root directory being served by this filesystem to a path in the local namespace
    /// and returns a [`BindGuard`] that unbinds the path when dropped.
    /// The path must be absolute, containing no "." nor ".." entries.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if binding failed.
    pub fn bind_to_path<'a>(&'a self, path: &str) -> Result<BindGuard<'a, FSC>, BindError> {
        let (client_end, server_end) = Channel::create().map_err(fidl::Error::ChannelPairCreate)?;
        self.root_dir.clone(CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_end))?;
        let namespace = fdio::Namespace::installed().map_err(BindError::LocalNamespace)?;
        namespace.bind(path, client_end).map_err(BindError::Bind)?;
        Ok(BindGuard { _filesystem: self, namespace, path: path.to_string() })
    }

    /// Attempts to shutdown the filesystem using the
    /// [`fidl_fuchsia_io::DirectoryProxy::unmount()`] FIDL method and waiting for the
    /// filesystem process to terminate.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the shutdown failed or the filesystem process did not terminate.
    /// The error type provides a way to recover from this error using the method
    /// [`ShutdownErrorAndFilesystem::kill()`].
    pub async fn shutdown(mut self) -> Result<Filesystem<FSC>, ShutdownErrorAndFilesystem<FSC>> {
        match self.do_shutdown().await {
            Ok(()) => Ok(self.filesystem.take().unwrap()),
            Err(shutdown_error) => {
                Err(ShutdownErrorAndFilesystem { shutdown_error, filesystem: self })
            }
        }
    }

    async fn do_shutdown(&mut self) -> Result<(), ShutdownError> {
        let admin_proxy: fidl_fuchsia_fs::AdminProxy =
            connect_to_protocol_at_dir_svc::<fidl_fuchsia_fs::AdminMarker>(&self.export_root)?;
        admin_proxy.shutdown().await?;

        let _ = OnSignals::new(&self.process, Signals::PROCESS_TERMINATED)
            .await
            .map_err(ShutdownError::ProcessTerminatedSignal)?;

        let info = self.process.info().map_err(ShutdownError::GetProcessReturnCode)?;
        if info.return_code != 0 {
            warn!("process returned non-zero exit code ({}) after shutdown", info.return_code);
        }

        // Prevent the drop impl from killing the process again.
        self.process = Handle::invalid().into();
        Ok(())
    }

    /// Returns a [`FilesystemInfo`] object containing information about the serving filesystem.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if querying the filesystem failed.
    pub async fn query(&self) -> Result<Box<FilesystemInfo>, QueryError> {
        let (status, info) = self.root_dir.query_filesystem().await?;
        Status::ok(status).map_err(QueryError::DirectoryQuery)?;
        info.ok_or(QueryError::DirectoryEmptyResult)
    }

    /// Attempts to kill the filesystem process and waits for the process to terminate.
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process could not be terminated. There is no way to
    /// recover the [`Filesystem`] from this error.
    pub async fn kill(mut self) -> Result<Filesystem<FSC>, KillError> {
        // Prevent the drop impl from killing the process again.
        let mut process = Handle::invalid().into();
        std::mem::swap(&mut self.process, &mut process);
        process.kill().map_err(KillError::TaskKill)?;
        let _ = OnSignals::new(&process, Signals::PROCESS_TERMINATED)
            .await
            .map_err(KillError::ProcessTerminatedSignal)?;
        Ok(self.filesystem.take().unwrap())
    }
}

impl<FSC> Drop for ServingFilesystem<FSC> {
    fn drop(&mut self) {
        if !self.process.is_invalid_handle() {
            let _ = self.process.kill();
        }
    }
}

/// A RAII wrapper returned from [`ServingFilesystem::bind_to_path()`] that unbinds a path when
/// dropped.
pub struct BindGuard<'a, FSC> {
    _filesystem: &'a ServingFilesystem<FSC>,
    namespace: fdio::Namespace,
    path: String,
}

impl<'a, FSC> Drop for BindGuard<'a, FSC> {
    fn drop(&mut self) {
        let _ = self.namespace.unbind(&self.path);
    }
}

async fn wait_for_successful_exit(process: Process) -> Result<(), CommandError> {
    let _ = OnSignals::new(&process, Signals::PROCESS_TERMINATED)
        .await
        .map_err(CommandError::ProcessTerminatedSignal)?;

    let info = process.info().map_err(CommandError::GetProcessReturnCode)?;
    if info.return_code == 0 {
        Ok(())
    } else {
        Err(CommandError::ProcessNonZeroReturnCode(info.return_code))
    }
}

/// The error type returned from [`Filesystem::serve()`].
///
/// Provides a method [`Self::into_filesystem()`] to retrieve the [`Filesystem`] that caused
/// the error.
pub struct ServeErrorAndFilesystem<FSC> {
    serve_error: ServeError,
    filesystem: Filesystem<FSC>,
}

impl<FSC> ServeErrorAndFilesystem<FSC> {
    /// Returns the error type representing the failure.
    pub fn serve_error(&self) -> &ServeError {
        &self.serve_error
    }

    /// Consumes this error and returns the [`Filesystem`] that caused it.
    pub fn into_filesystem(self) -> Filesystem<FSC> {
        self.filesystem
    }
}

impl<FSC> std::fmt::Debug for ServeErrorAndFilesystem<FSC> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ServeErrorAndFilesystem")
            .field("serve_error", &self.serve_error)
            .finish_non_exhaustive()
    }
}

/// The error type returned from [`ServingFilesystem::shutdown()`].
///
/// Provides a way to attempt to kill the serving filesystem process as a way to recover.
pub struct ShutdownErrorAndFilesystem<FSC> {
    shutdown_error: ShutdownError,
    filesystem: ServingFilesystem<FSC>,
}

impl<FSC: FSConfig> ShutdownErrorAndFilesystem<FSC> {
    /// Returns the error type representing the failure.
    pub fn shutdown_error(&self) -> &ShutdownError {
        &self.shutdown_error
    }

    /// Kills the filesystem process and returns the backing [`Filesystem`].
    ///
    /// # Errors
    ///
    /// Returns [`Err`] if the filesystem process could not be killed. The error type [`KillError`]
    /// does not offer a way to recover the backing [`Filesystem`].
    pub async fn kill_filesystem(self) -> Result<Filesystem<FSC>, KillError> {
        self.filesystem.kill().await
    }
}

impl<FSC> std::fmt::Debug for ShutdownErrorAndFilesystem<FSC> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("ShutdownErrorAndFilesystem")
            .field("shutdown_error", &self.shutdown_error)
            .finish_non_exhaustive()
    }
}

#[cfg(test)]
mod tests {
    use std::io::Read;

    use {
        super::*,
        crate::{BlobCompression, BlobEvictionPolicy, Blobfs, Factoryfs, Minfs},
        fidl_fuchsia_io as fio,
        ramdevice_client::RamdiskClient,
        std::io::{Seek, Write},
    };

    fn ramdisk(block_size: u64) -> RamdiskClient {
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(30),
        )
        .unwrap();
        RamdiskClient::create(block_size, 1 << 16).unwrap()
    }

    fn new_fs<FSC: FSConfig>(ramdisk: &RamdiskClient, config: FSC) -> Filesystem<FSC> {
        Filesystem::from_channel(ramdisk.open().unwrap(), config).unwrap()
    }

    #[fuchsia::test]
    async fn blobfs_custom_config() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let config = Blobfs {
            verbose: true,
            metrics: true,
            readonly: true,
            blob_deprecated_padded_format: false,
            blob_compression: Some(BlobCompression::Uncompressed),
            blob_eviction_policy: Some(BlobEvictionPolicy::EvictImmediately),
        };
        let blobfs = new_fs(&ramdisk, config);

        blobfs.format().await.expect("failed to format blobfs");
        blobfs.fsck().await.expect("failed to fsck blobfs");
        let _ = blobfs.serve().await.expect("failed to serve blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_format_fsck_success() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let blobfs = new_fs(&ramdisk, Blobfs::default());

        blobfs.format().await.expect("failed to format blobfs");
        blobfs.fsck().await.expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_format_fsck_error() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let blobfs = new_fs(&ramdisk, Blobfs::default());
        blobfs.format().await.expect("failed to format blobfs");
        let device_channel = ramdisk.open().expect("failed to get channel to device");

        // force fsck to fail by stomping all over one of blobfs's metadata blocks after formatting
        // TODO(fxbug.dev/35860): corrupt something other than the superblock
        {
            let mut file = fdio::create_fd::<std::fs::File>(device_channel.into_handle())
                .expect("failed to convert to file descriptor");
            let mut bytes: Vec<u8> = std::iter::repeat(0xff).take(block_size as usize).collect();
            file.write(&mut bytes).expect("failed to write to device");
        }

        blobfs.fsck().await.expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_format_serve_write_query_restart_read_shutdown() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let blobfs = new_fs(&ramdisk, Blobfs::default());

        blobfs.format().await.expect("failed to format blobfs");

        let serving = blobfs.serve().await.expect("failed to serve blobfs the first time");

        // snapshot of FilesystemInfo
        let fs_info1 =
            serving.query().await.expect("failed to query filesystem info after first serving");

        // pre-generated merkle test fixture data
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let content = String::from("test content").into_bytes();

        {
            let test_file = io_util::directory::open_file(
                serving.root(),
                merkle,
                fio::OPEN_FLAG_CREATE | fio::OPEN_RIGHT_WRITABLE,
            )
            .await
            .expect("failed to create test file");
            Status::ok(
                test_file
                    .truncate(content.len() as u64)
                    .await
                    .expect("failed to send truncate FIDL"),
            )
            .expect("failed to truncate file");
            let _: u64 = test_file
                .write(&content)
                .await
                .expect("failed to write to test file")
                .map_err(Status::from_raw)
                .expect("write error");
        }

        // check against the snapshot FilesystemInfo
        let fs_info2 = serving.query().await.expect("failed to query filesystem info after write");
        assert_eq!(
            fs_info2.used_bytes - fs_info1.used_bytes,
            fs_info2.block_size as u64 // assuming content < 8K
        );

        let blobfs = serving.shutdown().await.expect("failed to shutdown blobfs the first time");
        let serving = blobfs.serve().await.expect("failed to serve blobfs the second time");

        {
            let test_file =
                io_util::directory::open_file(serving.root(), merkle, fio::OPEN_RIGHT_READABLE)
                    .await
                    .expect("failed to open test file");
            let read_content =
                io_util::file::read(&test_file).await.expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        // once more check against the snapshot FilesystemInfo
        let fs_info3 = serving.query().await.expect("failed to query filesystem info after read");
        assert_eq!(
            fs_info3.used_bytes - fs_info1.used_bytes,
            fs_info3.block_size as u64 // assuming content < 8K
        );

        serving.shutdown().await.expect("failed to shutdown blobfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn blobfs_bind_to_path() {
        let block_size = 512;
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let test_content = b"test content";
        let ramdisk = ramdisk(block_size);
        let blobfs = new_fs(&ramdisk, Blobfs::default());

        blobfs.format().await.expect("failed to format blobfs");
        let serving = blobfs.serve().await.expect("failed to serve blobfs");
        let bind_guard = serving.bind_to_path("/test-blobfs-path").expect("bind_to_path failed");
        let test_path = format!("/test-blobfs-path/{}", merkle);

        {
            let mut file = std::fs::File::create(&test_path).expect("failed to create test file");
            file.set_len(test_content.len() as u64).expect("failed to set size");
            file.write_all(test_content).expect("write bytes");
        }

        {
            let mut file = std::fs::File::open(&test_path).expect("failed to open test file");
            let mut buf = Vec::new();
            file.read_to_end(&mut buf).expect("failed to read test file");
            assert_eq!(buf, test_content);
        }

        drop(bind_guard);

        std::fs::File::open(&test_path).expect_err("test file was not unbound");
    }

    #[fuchsia::test]
    async fn minfs_custom_config() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let config = Minfs {
            verbose: true,
            metrics: true,
            readonly: true,
            fsck_after_every_transaction: true,
        };
        let minfs = new_fs(&ramdisk, config);

        minfs.format().await.expect("failed to format minfs");
        minfs.fsck().await.expect("failed to fsck minfs");
        let _ = minfs.serve().await.expect("failed to serve minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_format_fsck_success() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");
        minfs.fsck().await.expect("failed to fsck minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_format_fsck_error() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");

        // force fsck to fail by stomping all over one of minfs's metadata blocks after formatting
        {
            let device_channel = ramdisk.open().expect("failed to get channel to device");
            let mut file = fdio::create_fd::<std::fs::File>(device_channel.into_handle())
                .expect("failed to convert to file descriptor");

            // when minfs isn't on an fvm, the location for it's bitmap offset is the 8th block.
            // TODO(fxbug.dev/35861): parse the superblock for this offset and the block size.
            let bitmap_block_offset = 8;
            let bitmap_offset = block_size * bitmap_block_offset;

            let mut stomping_bytes: Vec<u8> =
                std::iter::repeat(0xff).take(block_size as usize).collect();
            let actual_offset = file
                .seek(std::io::SeekFrom::Start(bitmap_offset))
                .expect("failed to seek to bitmap");
            assert_eq!(actual_offset, bitmap_offset);
            file.write(&mut stomping_bytes).expect("failed to write to device");
        }

        minfs.fsck().await.expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_format_serve_write_query_restart_read_shutdown() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");
        let serving = minfs.serve().await.expect("failed to serve minfs the first time");

        // snapshot of FilesystemInfo
        let fs_info1 =
            serving.query().await.expect("failed to query filesystem info after first serving");

        let filename = "test_file";
        let content = String::from("test content").into_bytes();

        {
            let test_file = io_util::directory::open_file(
                serving.root(),
                filename,
                fio::OPEN_FLAG_CREATE | fio::OPEN_RIGHT_WRITABLE,
            )
            .await
            .expect("failed to create test file");
            let _: u64 = test_file
                .write(&content)
                .await
                .expect("failed to write to test file")
                .map_err(Status::from_raw)
                .expect("write error");
        }

        // check against the snapshot FilesystemInfo
        let fs_info2 = serving.query().await.expect("failed to query filesystem info after write");
        assert_eq!(
            fs_info2.used_bytes - fs_info1.used_bytes,
            fs_info2.block_size as u64 // assuming content < 8K
        );

        let minfs = serving.shutdown().await.expect("failed to shutdown minfs the first time");
        let serving = minfs.serve().await.expect("failed to serve minfs the second time");

        {
            let test_file =
                io_util::directory::open_file(serving.root(), filename, fio::OPEN_RIGHT_READABLE)
                    .await
                    .expect("failed to open test file");
            let read_content =
                io_util::file::read(&test_file).await.expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        // once more check against the snapshot FilesystemInfo
        let fs_info3 = serving.query().await.expect("failed to query filesystem info after read");
        assert_eq!(
            fs_info3.used_bytes - fs_info1.used_bytes,
            fs_info3.block_size as u64 // assuming content < 8K
        );

        let _ = serving.shutdown().await.expect("failed to shutdown minfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn minfs_bind_to_path() {
        let block_size = 8192;
        let test_content = b"test content";
        let ramdisk = ramdisk(block_size);
        let minfs = new_fs(&ramdisk, Minfs::default());

        minfs.format().await.expect("failed to format minfs");
        let serving = minfs.serve().await.expect("failed to serve minfs");
        let bind_guard = serving.bind_to_path("/test-minfs-path").expect("bind_to_path failed");
        let test_path = "/test-minfs-path/test_file";

        {
            let mut file = std::fs::File::create(test_path).expect("failed to create test file");
            file.write_all(test_content).expect("write bytes");
        }

        {
            let mut file = std::fs::File::open(test_path).expect("failed to open test file");
            let mut buf = Vec::new();
            file.read_to_end(&mut buf).expect("failed to read test file");
            assert_eq!(buf, test_content);
        }

        drop(bind_guard);

        std::fs::File::open(test_path).expect_err("test file was not unbound");
    }

    #[fuchsia::test]
    async fn factoryfs_custom_config() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let config = Factoryfs { verbose: true, metrics: true };
        let factoryfs = new_fs(&ramdisk, config);

        factoryfs.format().await.expect("failed to format factoryfs");
        factoryfs.fsck().await.expect("failed to fsck factoryfs");
        let _ = factoryfs.serve().await.expect("failed to serve factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn factoryfs_format_fsck_success() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let factoryfs = new_fs(&ramdisk, Factoryfs::default());

        factoryfs.format().await.expect("failed to format factoryfs");
        factoryfs.fsck().await.expect("failed to fsck factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn factoryfs_format_serve_shutdown() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let factoryfs = new_fs(&ramdisk, Factoryfs::default());

        factoryfs.format().await.expect("failed to format factoryfs");
        let serving = factoryfs.serve().await.expect("failed to serve factoryfs");
        serving.shutdown().await.expect("failed to shutdown factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[fuchsia::test]
    async fn factoryfs_bind_to_path() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let factoryfs = new_fs(&ramdisk, Factoryfs::default());

        factoryfs.format().await.expect("failed to format factoryfs");
        let serving = factoryfs.serve().await.expect("failed to serve factoryfs");
        let bind_guard = serving.bind_to_path("/test-factoryfs-path").expect("bind_to_path failed");

        // factoryfs is read-only, so just check that we can open the root directory.
        {
            let file =
                std::fs::File::open("/test-factoryfs-path").expect("failed to open root directory");
            file.metadata().expect("failed to get metadata");
        }

        drop(bind_guard);

        std::fs::File::open("/test-factoryfs-path").expect_err("factoryfs path is still bound");
    }
}
