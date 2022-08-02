// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Library for filesystem management in rust.
//!
//! This library is analogous to the fs-management library in zircon. It provides support for
//! formatting, mounting, unmounting, and fsck-ing. It is implemented in a similar way to the C++
//! version.  For components v2, add `/svc/fuchsia.process.Launcher` to `use` and add the
//! binaries as dependencies to your component.

mod error;
pub mod filesystem;

use {
    anyhow::{bail, format_err, Context as _, Error},
    cstr::cstr,
    fdio::{service_connect, service_connect_at, spawn_etc, Namespace, SpawnAction, SpawnOptions},
    fidl::endpoints::DiscoverableProtocolMarker,
    fidl_fuchsia_fs::AdminSynchronousProxy,
    fidl_fuchsia_io as fio,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, Task},
    std::{ffi::CStr, sync::Arc},
};

// Re-export errors as public.
pub use error::{
    CommandError, KillError, LaunchProcessError, QueryError, ServeError, ShutdownError,
};

/// Constants for fuchsia.io/FilesystemInfo.fs_type
/// Keep in sync with VFS_TYPE_* types in //zircon/system/public/zircon/device/vfs.h
pub mod vfs_type {
    pub const BLOBFS: u32 = 0x9e694d21;
    pub const FATFS: u32 = 0xce694d21;
    pub const MINFS: u32 = 0x6e694d21;
    pub const MEMFS: u32 = 0x3e694d21;
    pub const FACTORYFS: u32 = 0x1e694d21;
    pub const FXFS: u32 = 0x73667866;
    pub const F2FS: u32 = 0xfe694d21;
}

// TODO(https://fxbug.dev/105241): Remove this.
/// Stores state of the mounted filesystem instance
struct FSInstance {
    process: zx::Process,
    mount_point: String,
    export_root: zx::Channel,
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
        crypt_client: Option<zx::Channel>,
    ) -> Result<Self, Error> {
        let (export_root, server_end) = fidl::endpoints::create_endpoints::<fio::NodeMarker>()?;
        let export_root = fio::DirectorySynchronousProxy::new(export_root.into_channel());

        let mut actions = vec![
            // export root handle is passed in as a PA_DIRECTORY_REQUEST handle at argument 0
            SpawnAction::add_handle(
                HandleInfo::new(HandleType::DirectoryRequest, 0),
                server_end.into(),
            ),
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];
        if let Some(crypt_client) = crypt_client {
            actions.push(SpawnAction::add_handle(
                HandleInfo::new(HandleType::User0, 2),
                crypt_client.into(),
            ));
        }
        let process = launch_process(&args, actions)?;

        // Wait until the filesystem is ready to take incoming requests. We want
        // mount errors to show before we bind to the namespace.
        let (root_dir, server_end) = fidl::endpoints::create_endpoints::<fio::NodeMarker>()?;
        export_root.open(
            fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::POSIX_EXECUTABLE
                | fio::OpenFlags::POSIX_WRITABLE,
            0,
            "root",
            server_end.into(),
        )?;
        let root_dir = fio::DirectorySynchronousProxy::new(root_dir.into_channel());
        let _: fio::NodeInfo = root_dir.describe(zx::Time::INFINITE).context("failed to mount")?;

        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        namespace
            .bind(mount_point, root_dir.into_channel())
            .context("failed to bind client channel into default namespace")?;

        Ok(Self {
            process,
            mount_point: mount_point.to_string(),
            export_root: export_root.into_channel(),
        })
    }

    /// Unmount the filesystem partition. The partition must already be mounted.
    fn unmount(self) -> Result<(), Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        service_connect_at(
            &self.export_root,
            fidl_fuchsia_fs::AdminMarker::PROTOCOL_NAME,
            server_chan,
        )?;
        let admin_proxy = AdminSynchronousProxy::new(client_chan);
        admin_proxy.shutdown(zx::Time::INFINITE)?;

        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        namespace
            .unbind(&self.mount_point)
            .context("failed to unbind filesystem from default namespace")
    }

    /// Get `FileSystemInfo` struct from which one can find out things like
    /// free space, used space, block size, etc.
    fn query_filesystem(&self) -> Result<Box<fio::FilesystemInfo>, Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        service_connect(&self.mount_point, server_chan)
            .context("failed to connect to filesystem")?;

        let proxy = fio::DirectorySynchronousProxy::new(client_chan);

        let (status, result) = proxy
            .query_filesystem(zx::Time::INFINITE)
            .context("failed to query filesystem info")?;
        zx::Status::ok(status).context("failed to query filesystem info")?;
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

fn launch_process(
    args: &[&CStr],
    mut actions: Vec<SpawnAction<'_>>,
) -> Result<zx::Process, LaunchProcessError> {
    match spawn_etc(
        &zx::Handle::invalid().into(),
        SpawnOptions::CLONE_ALL,
        args[0],
        args,
        None,
        &mut actions,
    ) {
        Ok(process) => Ok(process),
        Err((status, message)) => Err(LaunchProcessError {
            args: args.iter().map(|&a| a.to_owned()).collect(),
            status,
            message,
        }),
    }
}

/// Describes the configuration for a particular native filesystem.
pub trait FSConfig {
    /// If the filesystem runs as a component, Returns the component name in which case the
    /// binary_path, and the _args methods are not relevant.
    fn component_name(&self) -> Option<&str> {
        None
    }

    /// Path to the filesystem binary
    fn binary_path(&self) -> &CStr;

    /// Arguments passed to the binary for all subcommands
    fn generic_args(&self) -> Vec<&CStr> {
        vec![]
    }

    /// Arguments passed to the binary for formatting
    fn format_args(&self) -> Vec<&CStr> {
        vec![]
    }

    /// Arguments passed to the binary for mounting
    fn mount_args(&self) -> Vec<&CStr> {
        vec![]
    }

    /// Returns a handle for the crypt service (if any).
    fn crypt_client(&self) -> Option<zx::Channel> {
        // By default, filesystems don't need a crypt service.
        None
    }
}

// TODO(https://fxbug.dev/105241): Remove this.
/// Manages a block device for filesystem operations
pub struct Filesystem<FSC: FSConfig> {
    device: fio::NodeSynchronousProxy,
    config: FSC,
    instance: Option<FSInstance>,
}

impl<FSC: FSConfig> Filesystem<FSC> {
    /// Manage a filesystem on a device at the given path. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn from_path(device_path: &str, config: FSC) -> Result<Self, Error> {
        let (client_end, server_end) = zx::Channel::create()?;
        service_connect(device_path, server_end).context("could not connect to block device")?;
        Self::from_channel(client_end, config)
    }

    /// Manage a filesystem on a device at the given channel. The device is not formatted, mounted,
    /// or modified at this point.
    pub fn from_channel(client_end: zx::Channel, config: FSC) -> Result<Self, Error> {
        let device = fio::NodeSynchronousProxy::new(client_end);
        Ok(Self { device, config, instance: None })
    }

    /// Returns a channel to the block device.
    fn get_channel(&mut self) -> Result<zx::Channel, Error> {
        let (channel, server) = zx::Channel::create()?;
        let () = self
            .device
            .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, fidl::endpoints::ServerEnd::new(server))?;
        Ok(channel)
    }

    /// Mount the provided block device and bind it to the provided mount_point in the default
    /// namespace. The filesystem can't already be mounted, and the mount will fail if the provided
    /// mount path doesn't already exist. The path is relative to the root of the default namespace,
    /// and can't contain any '.' or '..' entries.
    pub fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        if self.instance.is_some() {
            bail!("cannot mount. filesystem is already mounted");
        }
        if self.config.component_name().is_some() {
            bail!("Not supported");
        }

        let block_device = self.get_channel()?;

        let mut args = vec![self.config.binary_path()];
        args.append(&mut self.config.generic_args());
        args.push(cstr!("mount"));
        args.append(&mut self.config.mount_args());

        self.instance =
            Some(FSInstance::mount(block_device, args, mount_point, self.config.crypt_client())?);

        Ok(())
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
    pub fn query_filesystem(&self) -> Result<Box<fio::FilesystemInfo>, Error> {
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
#[derive(Clone)]
pub enum BlobLayout {
    /// Merkle tree is stored in a separate block. This is deprecated and used only on Astro
    /// devices (it takes more space).
    DeprecatedPadded,

    /// Merkle tree is appended to the last block of data
    Compact,
}

/// Compression used for blobs in blobfs
#[derive(Clone)]
pub enum BlobCompression {
    ZSTD,
    ZSTDSeekable,
    ZSTDChunked,
    Uncompressed,
}

/// Eviction policy used for blobs in blobfs
#[derive(Clone)]
pub enum BlobEvictionPolicy {
    NeverEvict,
    EvictImmediately,
}

/// Blobfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Clone, Default)]
pub struct Blobfs {
    pub verbose: bool,
    pub readonly: bool,
    pub blob_deprecated_padded_format: bool,
    pub blob_compression: Option<BlobCompression>,
    pub blob_eviction_policy: Option<BlobEvictionPolicy>,
}

impl Blobfs {
    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(path: &str) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_path(path, Self::default())
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(channel: zx::Channel) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_channel(channel, Self::default())
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
        if self.blob_deprecated_padded_format {
            args.push(cstr!("--deprecated_padded_format"));
        }
        args
    }
    fn mount_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.readonly {
            args.push(cstr!("--readonly"));
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
#[derive(Clone, Default)]
pub struct Minfs {
    // TODO(xbhatnag): Add support for fvm_data_slices
    pub verbose: bool,
    pub readonly: bool,
    pub fsck_after_every_transaction: bool,
}

impl Minfs {
    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(path: &str) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_path(path, Self::default())
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(channel: zx::Channel) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_channel(channel, Self::default())
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
    fn mount_args(&self) -> Vec<&CStr> {
        let mut args = vec![];
        if self.readonly {
            args.push(cstr!("--readonly"));
        }
        if self.fsck_after_every_transaction {
            args.push(cstr!("--fsck_after_every_transaction"));
        }
        args
    }
}

type CryptClientFn = Arc<dyn Fn() -> zx::Channel + Send + Sync>;

/// Fxfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Clone)]
pub struct Fxfs {
    pub verbose: bool,
    pub readonly: bool,
    pub crypt_client_fn: CryptClientFn,
}

impl Fxfs {
    pub fn with_crypt_client(crypt_client_fn: CryptClientFn) -> Self {
        Fxfs { verbose: false, readonly: false, crypt_client_fn }
    }

    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(
        path: &str,
        crypt_client_fn: CryptClientFn,
    ) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_path(path, Self::with_crypt_client(crypt_client_fn))
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(
        channel: zx::Channel,
        crypt_client_fn: CryptClientFn,
    ) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_channel(channel, Self::with_crypt_client(crypt_client_fn))
    }
}

impl FSConfig for Fxfs {
    fn component_name(&self) -> Option<&str> {
        Some("fxfs")
    }
    fn binary_path(&self) -> &CStr {
        cstr!("")
    }
    fn crypt_client(&self) -> Option<zx::Channel> {
        Some((self.crypt_client_fn)())
    }
}

/// Factoryfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Clone, Default)]
pub struct Factoryfs {
    pub verbose: bool,
}

impl Factoryfs {
    /// Manages a block device at a given path using
    /// the default configuration.
    pub fn new(path: &str) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_path(path, Self::default())
    }

    /// Manages a block device at a given channel using
    /// the default configuration.
    pub fn from_channel(channel: zx::Channel) -> Result<filesystem::Filesystem<Self>, Error> {
        filesystem::Filesystem::from_channel(channel, Self::default())
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
}
