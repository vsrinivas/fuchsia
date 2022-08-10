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
    anyhow::Error,
    cstr::cstr,
    fdio::{spawn_etc, SpawnAction, SpawnOptions},
    fuchsia_zircon as zx,
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

    /// Whether the filesystem supports multiple volumes.
    fn is_multi_volume(&self) -> bool {
        false
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
    fn is_multi_volume(&self) -> bool {
        true
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
