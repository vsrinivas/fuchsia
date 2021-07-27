// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for filesystem management in rust.
//!
//! This library is analogous to the fs-management library in zircon. It provides support for
//! formatting, mounting, unmounting, and fsck-ing. It is implemented in a similar way to the C++
//! version - it uses the blobfs command line tool present in the base image. In order to use this
//! library inside of a sandbox, the following must be added to the relevant component manifest
//! file -
//!
//! ```
//! "sandbox": {
//!     "services": [
//!         "fuchsia.process.Launcher",
//!         "fuchsia.tracing.provider.Registry"
//!     ]
//! }
//! ```
//!
//! and the projects BUILD.gn file must contain
//!
//! ```
//! package("foo") {
//!     deps = [
//!         "//src/storage/bin/blobfs",
//!         "//src/storage/bin/minfs",
//!         ...
//!     ]
//!     binaries = [
//!         { name = "blobfs" },
//!         { name = "minfs" },
//!         ...
//!     ]
//!     ...
//! }
//! ```
//!
//! for components v1. For components v2, add `/svc/fuchsia.process.Launcher` to `use` and add the
//! binaries as dependencies to your component.
//!
//! This library currently doesn't work outside of a component (the filesystem utility binary paths
//! are hard-coded strings).

use {
    anyhow::{format_err, Context as _, Error},
    cstr::cstr,
    fdio::{spawn_etc, Namespace, SpawnAction, SpawnOptions},
    fidl_fuchsia_io::{
        DirectoryAdminSynchronousProxy, FilesystemInfo, NodeSynchronousProxy,
        CLONE_FLAG_SAME_RIGHTS, OPEN_RIGHT_ADMIN,
    },
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, AsHandleRef, Task},
    fuchsia_zircon_status as zx_status,
    std::ffi::CStr,
};

/// Stores state of the mounted filesystem instance
struct FSInstance {
    process: zx::Process,
    mount_point: String,
}

impl FSInstance {
    /// Mount the filesystem partition that exists on the provided block device, allowing it to
    /// receive requests on the root channel. In order to be mounted in the traditional sense, the
    /// client side of the provided root channel needs to be bound to a path in a namespace
    /// somewhere.
    fn mount(
        block_device: zx::Channel,
        args: Vec<&CStr>,
        mount_point: &str,
    ) -> Result<Self, Error> {
        let (node, server_end) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::NodeMarker>()?;
        let node = NodeSynchronousProxy::new(node.into_channel());

        let actions = vec![
            // root handle is passed in as a PA_USER0 handle at argument 0
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 0), server_end.into()),
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        let process = launch_process(&args, actions)?;

        // Wait until the filesystem is ready to take incoming requests. We want
        // mount errors to show before we bind to the namespace.
        let _: fidl_fuchsia_io::NodeInfo =
            node.describe(zx::Time::INFINITE).context("failed to mount")?;

        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        namespace
            .bind(mount_point, node.into_channel())
            .context("failed to bind client channel into default namespace")?;

        Ok(Self { process, mount_point: mount_point.to_string() })
    }

    /// Unmount the filesystem partition. The partition must already be mounted.
    fn unmount(self) -> Result<(), Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        namespace
            .connect(&self.mount_point, OPEN_RIGHT_ADMIN, server_chan)
            .context("failed to connect to filesystem")?;

        let proxy = DirectoryAdminSynchronousProxy::new(client_chan);
        proxy.unmount(zx::Time::INFINITE).context("failed to unmount")?;

        namespace
            .unbind(&self.mount_point)
            .context("failed to unbind filesystem from default namespace")
    }

    /// Get `FileSystemInfo` struct from which one can find out things like
    /// free space, used space, block size, etc.
    fn query_filesystem(&self) -> Result<Box<FilesystemInfo>, Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        namespace
            .connect(&self.mount_point, OPEN_RIGHT_ADMIN, server_chan)
            .context("failed to connect to filesystem")?;

        let proxy = DirectoryAdminSynchronousProxy::new(client_chan);

        let (status, result) = proxy
            .query_filesystem(zx::Time::INFINITE)
            .context("failed to query filesystem info")?;
        zx_status::Status::ok(status).context("failed to query filesystem info")?;
        result.ok_or(format_err!("querying filesystem info got empty result"))
    }

    /// Terminate the filesystem process and force unmount the mount point
    fn kill(self) -> Result<(), Error> {
        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        namespace
            .unbind(&self.mount_point)
            .context("failed to unbind filesystem from default namespace")?;

        self.process.kill().context("Could not kill filesystem process")
    }
}

fn launch_process(args: &[&CStr], mut actions: Vec<SpawnAction<'_>>) -> Result<zx::Process, Error> {
    let options = SpawnOptions::CLONE_ALL;

    let process = match spawn_etc(
        &zx::Handle::invalid().into(),
        options,
        args[0],
        args,
        None,
        &mut actions,
    ) {
        Ok(process) => process,
        Err((status, message)) => {
            return Err(format_err!(
                "failed to spawn process. launched with: {:?}, status: {}, message: {}",
                args,
                status,
                message
            ));
        }
    };

    Ok(process)
}

fn run_command_and_wait_for_clean_exit(
    args: Vec<&CStr>,
    block_device: zx::Channel,
) -> Result<(), Error> {
    let actions = vec![
        // device handle is passed in as a PA_USER0 handle at argument 1
        SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
    ];

    let process = launch_process(&args, actions)?;

    let _signals = process
        .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
        .context(format!("failed to wait for process to complete"))?;

    let info = process.info().context("failed to get process info")?;
    if !zx::ProcessInfoFlags::from_bits(info.flags).unwrap().contains(zx::ProcessInfoFlags::EXITED)
        || info.return_code != 0
    {
        return Err(format_err!("process returned non-zero exit code ({})", info.return_code));
    }

    Ok(())
}

/// Describes the configuration for a particular native filesystem.
pub trait FSConfig {
    /// Path to the filesystem binary
    fn binary_path(&self) -> &CStr;

    /// Arguments passed to the binary for all subcommands
    fn generic_args(&self) -> Vec<&CStr>;

    /// Arguments passed to the binary for formatting
    fn format_args(&self) -> Vec<&CStr>;

    /// Arguments passed to the binary for mounting
    fn mount_args(&self) -> Vec<&CStr>;
}

/// Manages a block device for filesystem operations
pub struct Filesystem<FSC: FSConfig> {
    device: NodeSynchronousProxy,
    config: FSC,
    instance: Option<FSInstance>,
}

impl<FSC: FSConfig> Filesystem<FSC> {
    /// Manage a filesystem on a device at the given path. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn from_path(device_path: &str, config: FSC) -> Result<Self, Error> {
        let (client_end, server_end) = zx::Channel::create()?;
        fdio::service_connect(device_path, server_end)
            .context("could not connect to block device")?;
        Self::from_channel(client_end, config)
    }

    /// Manage a filesystem on a device at the given channel. The device is not formatted, mounted,
    /// or modified at this point.
    pub fn from_channel(client_end: zx::Channel, config: FSC) -> Result<Self, Error> {
        let device = NodeSynchronousProxy::new(client_end);
        Ok(Self { device, config, instance: None })
    }

    /// Returns a channel to the block device.
    fn get_channel(&mut self) -> Result<zx::Channel, Error> {
        let (channel, server) = zx::Channel::create()?;
        let () =
            self.device.clone(CLONE_FLAG_SAME_RIGHTS, fidl::endpoints::ServerEnd::new(server))?;
        Ok(channel)
    }

    /// Mount the provided block device and bind it to the provided mount_point in the default
    /// namespace. The filesystem can't already be mounted, and the mount will fail if the provided
    /// mount path doesn't already exist. The path is relative to the root of the default namespace,
    /// and can't contain any '.' or '..' entries.
    pub fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        if self.instance.is_some() {
            return Err(format_err!("cannot mount. filesystem is already mounted"));
        }

        let block_device = self.get_channel()?;

        let mut args = vec![self.config.binary_path(), cstr!("mount")];
        args.append(&mut self.config.generic_args());
        args.append(&mut self.config.mount_args());

        self.instance = Some(FSInstance::mount(block_device, args, mount_point)?);

        Ok(())
    }

    /// Format the associated device with a fresh filesystem. It must not be mounted.
    pub fn format(&mut self) -> Result<(), Error> {
        if self.instance.is_some() {
            return Err(format_err!("cannot format! filesystem is mounted"));
        }

        let block_device = self.get_channel()?;

        let mut args = vec![self.config.binary_path(), cstr!("mkfs")];
        args.append(&mut self.config.generic_args());
        args.append(&mut self.config.format_args());

        run_command_and_wait_for_clean_exit(args, block_device).context("failed to format device")
    }

    /// Run fsck on the filesystem partition. Returns Ok(()) if fsck succeeds, or the associated
    /// error if it doesn't. Will fail if run on a mounted partition.
    pub fn fsck(&mut self) -> Result<(), Error> {
        if self.instance.is_some() {
            return Err(format_err!("cannot fsck! filesystem is mounted"));
        }

        let block_device = self.get_channel()?;

        let mut args = vec![self.config.binary_path(), cstr!("fsck")];
        args.append(&mut self.config.generic_args());

        run_command_and_wait_for_clean_exit(args, block_device).context("failed to fsck device")
    }

    /// Unmount the filesystem partition. The partition must already be mounted.
    pub fn unmount(&mut self) -> Result<(), Error> {
        if let Some(instance) = self.instance.take() {
            instance.unmount()
        } else {
            Err(format_err!("cannot unmount. filesystem is not mounted"))
        }
    }

    /// Get `FileSystemInfo` struct from which one can find out things like
    /// free space, used space, block size, etc.
    pub fn query_filesystem(&self) -> Result<Box<FilesystemInfo>, Error> {
        if let Some(instance) = &self.instance {
            instance.query_filesystem()
        } else {
            Err(format_err!("cannot query filesystem. filesystem is not mounted"))
        }
    }

    /// Terminate the filesystem process and force unmount the mount point
    pub fn kill(&mut self) -> Result<(), Error> {
        if let Some(instance) = self.instance.take() {
            instance.kill()
        } else {
            Err(format_err!("cannot kill. filesystem is not mounted"))
        }
    }
}

impl<FSC: FSConfig> Drop for Filesystem<FSC> {
    fn drop(&mut self) {
        if self.instance.is_some() {
            // Unmount if possible.
            let _ = self.unmount();
        }
    }
}

///
/// FILESYSTEMS
///

/// Layout of blobs in blobfs
pub enum BlobLayout {
    /// Merkle tree is stored in a separate block
    Padded,
    /// Merkle tree is appended to the last block of data
    Compact,
}

/// Compression used for blobs in blobfs
pub enum BlobCompression {
    ZSTD,
    ZSTDSeekable,
    ZSTDChunked,
    Uncompressed,
}

/// Eviction policy used for blobs in blobfs
pub enum BlobEvictionPolicy {
    NeverEvict,
    EvictImmediately,
}

/// Blobfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Default)]
pub struct Blobfs {
    pub verbose: bool,
    pub readonly: bool,
    pub metrics: bool,
    pub blob_layout: Option<BlobLayout>,
    pub blob_compression: Option<BlobCompression>,
    pub blob_eviction_policy: Option<BlobEvictionPolicy>,
}

impl Blobfs {
    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(path: &str) -> Result<Filesystem<Self>, Error> {
        Filesystem::from_path(path, Self::default())
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(channel: zx::Channel) -> Result<Filesystem<Self>, Error> {
        Filesystem::from_channel(channel, Self::default())
    }
}

impl FSConfig for Blobfs {
    fn binary_path(&self) -> &CStr {
        cstr!("/pkg/bin/blobfs")
    }
    fn generic_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.verbose {
            args.push(cstr!("--verbose"));
        }
        args
    }
    fn format_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if let Some(layout) = &self.blob_layout {
            args.push(cstr!("--blob_layout_format"));
            args.push(match layout {
                BlobLayout::Padded => cstr!("padded"),
                BlobLayout::Compact => cstr!("compact"),
            });
        }
        args
    }
    fn mount_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.readonly {
            args.push(cstr!("--readonly"));
        }
        if self.metrics {
            args.push(cstr!("--metrics"));
        }
        if let Some(compression) = &self.blob_compression {
            args.push(cstr!("--compression"));
            args.push(match compression {
                BlobCompression::ZSTD => cstr!("ZSTD"),
                BlobCompression::ZSTDSeekable => cstr!("ZSTD_SEEKABLE"),
                BlobCompression::ZSTDChunked => cstr!("ZSTD_CHUNKED"),
                BlobCompression::Uncompressed => cstr!("UNCOMPRESSED"),
            });
        }
        if let Some(eviction_policy) = &self.blob_eviction_policy {
            args.push(cstr!("--eviction_policy"));
            args.push(match eviction_policy {
                BlobEvictionPolicy::NeverEvict => cstr!("NEVER_EVICT"),
                BlobEvictionPolicy::EvictImmediately => cstr!("EVICT_IMMEDIATELY"),
            })
        }
        args
    }
}

/// Minfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Default)]
pub struct Minfs {
    // TODO(xbhatnag): Add support for fvm_data_slices
    pub verbose: bool,
    pub readonly: bool,
    pub metrics: bool,
    pub fsck_after_every_transaction: bool,
}

impl Minfs {
    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(path: &str) -> Result<Filesystem<Self>, Error> {
        Filesystem::from_path(path, Self::default())
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(channel: zx::Channel) -> Result<Filesystem<Self>, Error> {
        Filesystem::from_channel(channel, Self::default())
    }
}

impl FSConfig for Minfs {
    fn binary_path(&self) -> &CStr {
        cstr!("/pkg/bin/minfs")
    }
    fn generic_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.verbose {
            args.push(cstr!("--verbose"));
        }
        args
    }
    fn format_args(&self) -> Vec<&CStr> {
        vec![]
    }
    fn mount_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.readonly {
            args.push(cstr!("--readonly"));
        }
        if self.metrics {
            args.push(cstr!("--metrics"));
        }
        if self.fsck_after_every_transaction {
            args.push(cstr!("--fsck_after_every_transaction"));
        }
        args
    }
}

/// Factoryfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Default)]
pub struct Factoryfs {
    pub verbose: bool,
    pub metrics: bool,
}

impl Factoryfs {
    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(path: &str) -> Result<Filesystem<Self>, Error> {
        Filesystem::from_path(path, Self::default())
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(channel: zx::Channel) -> Result<Filesystem<Self>, Error> {
        Filesystem::from_channel(channel, Self::default())
    }
}

impl FSConfig for Factoryfs {
    fn binary_path(&self) -> &CStr {
        cstr!("/pkg/bin/factoryfs")
    }
    fn generic_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.verbose {
            args.push(cstr!("--verbose"));
        }
        args
    }
    fn format_args(&self) -> Vec<&CStr> {
        vec![]
    }
    fn mount_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.metrics {
            args.push(cstr!("--metrics"));
        }
        args
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            BlobCompression, BlobEvictionPolicy, BlobLayout, Blobfs, Factoryfs, Filesystem, Minfs,
        },
        fuchsia_zircon::HandleBased,
        ramdevice_client::RamdiskClient,
        std::io::{Read, Seek, Write},
    };

    fn ramdisk(block_size: u64) -> RamdiskClient {
        isolated_driver_manager::launch_isolated_driver_manager().unwrap();
        ramdevice_client::wait_for_device(
            "/dev/sys/platform/00:00:2d/ramctl",
            std::time::Duration::from_secs(30),
        )
        .unwrap();
        RamdiskClient::create(block_size, 1 << 16).unwrap()
    }

    fn blobfs(ramdisk: &RamdiskClient) -> Filesystem<Blobfs> {
        let device = ramdisk.open().unwrap();
        Blobfs::from_channel(device).unwrap()
    }

    #[test]
    fn blobfs_custom_config() {
        let block_size = 512;
        let mount_point = "/test-fs-root";

        let ramdisk = ramdisk(block_size);
        let device = ramdisk.open().unwrap();
        let config = Blobfs {
            verbose: true,
            metrics: true,
            readonly: true,
            blob_layout: Some(BlobLayout::Compact),
            blob_compression: Some(BlobCompression::Uncompressed),
            blob_eviction_policy: Some(BlobEvictionPolicy::EvictImmediately),
        };
        let mut blobfs = Filesystem::from_channel(device, config).unwrap();

        blobfs.format().expect("failed to format blobfs");
        blobfs.fsck().expect("failed to fsck blobfs");
        blobfs.mount(mount_point).expect("failed to mount blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_format_fsck_success() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut blobfs = blobfs(&ramdisk);

        blobfs.format().expect("failed to format blobfs");
        blobfs.fsck().expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_format_fsck_error() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut blobfs = blobfs(&ramdisk);

        blobfs.format().expect("failed to format blobfs");

        // force fsck to fail by stomping all over one of blobfs's metadata blocks after formatting
        // TODO(fxbug.dev/35860): corrupt something other than the superblock
        let device_channel = ramdisk.open().expect("failed to get channel to device");
        let mut file = fdio::create_fd::<std::fs::File>(device_channel.into_handle())
            .expect("failed to convert to file descriptor");
        let mut bytes: Vec<u8> = std::iter::repeat(0xff).take(block_size as usize).collect();
        file.write(&mut bytes).expect("failed to write to device");

        blobfs.fsck().expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_format_mount_write_query_remount_read_unmount() {
        let block_size = 512;
        let mount_point = "/test-fs-root";
        let ramdisk = ramdisk(block_size);
        let mut blobfs = blobfs(&ramdisk);

        blobfs.format().expect("failed to format blobfs");
        blobfs.mount(mount_point).expect("failed to mount blobfs the first time");

        // snapshot of FilesystemInfo
        let fs_info1 =
            blobfs.query_filesystem().expect("failed to query filesystem info after first mount");

        // pre-generated merkle test fixture data
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let content = String::from("test content").into_bytes();
        let path = format!("{}/{}", mount_point, merkle);

        {
            let mut test_file = std::fs::File::create(&path).expect("failed to create test file");
            test_file.set_len(content.len() as u64).expect("failed to truncate file");
            test_file.write(&content).expect("failed to write to test file");
        }

        // check against the snapshot FilesystemInfo
        let fs_info2 =
            blobfs.query_filesystem().expect("failed to query filesystem info after write");
        assert_eq!(
            fs_info2.used_bytes - fs_info1.used_bytes,
            fs_info2.block_size as u64 // assuming content < 8K
        );

        blobfs.unmount().expect("failed to unmount blobfs the first time");

        blobfs
            .query_filesystem()
            .expect_err("filesystem query on an unmounted filesystem didn't fail");

        blobfs.mount(mount_point).expect("failed to mount blobfs the second time");

        {
            let mut test_file = std::fs::File::open(&path).expect("failed to open test file");
            let mut read_content = Vec::new();
            test_file.read_to_end(&mut read_content).expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        // once more check against the snapshot FilesystemInfo
        let fs_info3 =
            blobfs.query_filesystem().expect("failed to query filesystem info after read");
        assert_eq!(
            fs_info3.used_bytes - fs_info1.used_bytes,
            fs_info3.block_size as u64 // assuming content < 8K
        );

        blobfs.unmount().expect("failed to unmount blobfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    fn minfs(ramdisk: &RamdiskClient) -> Filesystem<Minfs> {
        let device = ramdisk.open().unwrap();
        Minfs::from_channel(device).unwrap()
    }

    #[test]
    fn minfs_custom_config() {
        let block_size = 512;
        let mount_point = "/test-fs-root";

        let ramdisk = ramdisk(block_size);
        let device = ramdisk.open().unwrap();
        let config = Minfs {
            verbose: true,
            metrics: true,
            readonly: true,
            fsck_after_every_transaction: true,
        };
        let mut minfs = Filesystem::from_channel(device, config).unwrap();

        minfs.format().expect("failed to format minfs");
        minfs.fsck().expect("failed to fsck minfs");
        minfs.mount(mount_point).expect("failed to mount minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn minfs_format_fsck_success() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let mut minfs = minfs(&ramdisk);

        minfs.format().expect("failed to format minfs");
        minfs.fsck().expect("failed to fsck minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn minfs_format_fsck_error() {
        let block_size = 8192;
        let ramdisk = ramdisk(block_size);
        let mut minfs = minfs(&ramdisk);

        minfs.format().expect("failed to format minfs");

        // force fsck to fail by stomping all over one of minfs's metadata blocks after formatting
        let device_channel = ramdisk.open().expect("failed to get channel to device");
        let mut file = fdio::create_fd::<std::fs::File>(device_channel.into_handle())
            .expect("failed to convert to file descriptor");

        // when minfs isn't on an fvm, the location for it's bitmap offset is the 8th block.
        // TODO(fxbug.dev/35861): parse the superblock for this offset and the block size.
        let bitmap_block_offset = 8;
        let bitmap_offset = block_size * bitmap_block_offset;

        let mut stomping_bytes: Vec<u8> =
            std::iter::repeat(0xff).take(block_size as usize).collect();
        let actual_offset =
            file.seek(std::io::SeekFrom::Start(bitmap_offset)).expect("failed to seek to bitmap");
        assert_eq!(actual_offset, bitmap_offset);
        file.write(&mut stomping_bytes).expect("failed to write to device");

        minfs.fsck().expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn minfs_format_mount_write_query_remount_read_unmount() {
        let block_size = 8192;
        let mount_point = "/test-fs-root";
        let ramdisk = ramdisk(block_size);
        let mut minfs = minfs(&ramdisk);

        minfs.format().expect("failed to format minfs");
        minfs.mount(mount_point).expect("failed to mount minfs the first time");

        // snapshot of FilesystemInfo
        let fs_info1 =
            minfs.query_filesystem().expect("failed to query filesystem info after first mount");

        let filename = "test_file";
        let content = String::from("test content").into_bytes();
        let path = format!("{}/{}", mount_point, filename);

        {
            let mut test_file = std::fs::File::create(&path).expect("failed to create test file");
            test_file.write(&content).expect("failed to write to test file");
        }

        // check against the snapshot FilesystemInfo
        let fs_info2 =
            minfs.query_filesystem().expect("failed to query filesystem info after write");
        assert_eq!(
            fs_info2.used_bytes - fs_info1.used_bytes,
            fs_info2.block_size as u64 // assuming content < 8K
        );

        minfs.unmount().expect("failed to unmount minfs the first time");

        minfs
            .query_filesystem()
            .expect_err("filesystem query on an unmounted filesystem didn't fail");

        minfs.mount(mount_point).expect("failed to mount minfs the second time");

        {
            let mut test_file = std::fs::File::open(&path).expect("failed to open test file");
            let mut read_content = Vec::new();
            test_file.read_to_end(&mut read_content).expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        // once more check against the snapshot FilesystemInfo
        let fs_info3 =
            minfs.query_filesystem().expect("failed to query filesystem info after read");
        assert_eq!(
            fs_info3.used_bytes - fs_info1.used_bytes,
            fs_info3.block_size as u64 // assuming content < 8K
        );

        minfs.unmount().expect("failed to unmount minfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    fn factoryfs(ramdisk: &RamdiskClient) -> Filesystem<Factoryfs> {
        let device = ramdisk.open().unwrap();
        Factoryfs::from_channel(device).unwrap()
    }

    #[test]
    fn factoryfs_custom_config() {
        let block_size = 512;
        let mount_point = "/test-fs-root";

        let ramdisk = ramdisk(block_size);
        let device = ramdisk.open().unwrap();
        let config = Factoryfs { verbose: true, metrics: true };
        let mut factoryfs = Filesystem::from_channel(device, config).unwrap();

        factoryfs.format().expect("failed to format factoryfs");
        factoryfs.fsck().expect("failed to fsck factoryfs");
        factoryfs.mount(mount_point).expect("failed to mount factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn factoryfs_format_fsck_success() {
        let block_size = 512;
        let ramdisk = ramdisk(block_size);
        let mut factoryfs = factoryfs(&ramdisk);

        factoryfs.format().expect("failed to format factoryfs");
        factoryfs.fsck().expect("failed to fsck factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn factoryfs_format_mount_unmount() {
        let block_size = 512;
        let mount_point = "/test-fs-root";
        let ramdisk = ramdisk(block_size);
        let mut factoryfs = factoryfs(&ramdisk);

        factoryfs.format().expect("failed to format factoryfs");
        factoryfs.mount(mount_point).expect("failed to mount factoryfs");
        factoryfs.unmount().expect("failed to unmount factoryfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }
}
