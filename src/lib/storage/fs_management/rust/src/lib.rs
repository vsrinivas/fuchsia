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
pub mod format;

use {
    anyhow::Error,
    cstr::cstr,
    fdio::{spawn_etc, SpawnAction, SpawnOptions},
    fuchsia_zircon as zx,
    std::{ffi::CStr, sync::Arc},
};

// Re-export errors as public.
pub use error::{CommandError, KillError, LaunchProcessError, QueryError, ShutdownError};

pub const BLOBFS_TYPE_GUID: [u8; 16] = [
    0x0e, 0x38, 0x67, 0x29, 0x4c, 0x13, 0xbb, 0x4c, 0xb6, 0xda, 0x17, 0xe7, 0xce, 0x1c, 0xa4, 0x5d,
];
pub const DATA_TYPE_GUID: [u8; 16] = [
    0x0c, 0x5f, 0x18, 0x08, 0x2d, 0x89, 0x8a, 0x42, 0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a,
];

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

pub enum Mode<'a> {
    /// Run the filesystem as a legacy binary.
    Legacy(LegacyConfig<'a>),

    /// Run the filesystem as a component.
    Component {
        /// For static children, the name specifies the name of the child.  For dynamic children,
        /// the component URL is "#meta/{component-name}.cm".  The library will attempt to connect
        /// to a static child first, and if that fails, it will launch the filesystem within a
        /// collection.
        name: &'a str,

        /// It should be possible to reuse components after serving them, but it's not universally
        /// supported.
        reuse_component_after_serving: bool,
    },
}

impl<'a> Mode<'a> {
    fn into_legacy_config(self) -> Option<LegacyConfig<'a>> {
        match self {
            Mode::Legacy(config) => Some(config),
            _ => None,
        }
    }

    fn component_name(&self) -> Option<&str> {
        match self {
            Mode::Component { name, .. } => Some(name),
            _ => None,
        }
    }
}

#[derive(Default)]
pub struct LegacyConfig<'a> {
    /// Path to the binary.
    pub binary_path: &'a CStr,

    /// Arguments passed to the binary for all subcommands
    pub generic_args: Vec<&'a CStr>,

    /// Arguments passed to the binary for formatting
    pub format_args: Vec<&'a CStr>,

    /// Arguments passed to the binary for mounting
    pub mount_args: Vec<&'a CStr>,
}

/// Describes the configuration for a particular filesystem.
pub trait FSConfig {
    /// Returns the mode in which to run this filesystem.
    fn mode(&self) -> Mode<'_>;

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
    fn mode(&self) -> Mode<'_> {
        Mode::Component { name: "blobfs", reuse_component_after_serving: false }
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
    fn mode(&self) -> Mode<'_> {
        Mode::Legacy(LegacyConfig {
            binary_path: cstr!("/pkg/bin/minfs"),
            generic_args: {
                let mut args = vec![];
                if self.verbose {
                    args.push(cstr!("--verbose"));
                }
                args
            },
            mount_args: {
                let mut args = vec![];
                if self.readonly {
                    args.push(cstr!("--readonly"));
                }
                if self.fsck_after_every_transaction {
                    args.push(cstr!("--fsck_after_every_transaction"));
                }
                args
            },
            ..Default::default()
        })
    }
}

pub type CryptClientFn = Arc<dyn Fn() -> zx::Channel + Send + Sync>;

/// Fxfs Filesystem Configuration
/// If fields are None or false, they will not be set in arguments.
#[derive(Clone, Default)]
pub struct Fxfs {
    pub verbose: bool,
    pub readonly: bool,

    // This is only used by fsck.
    pub crypt_client_fn: Option<CryptClientFn>,
}

impl Fxfs {
    pub fn with_crypt_client(crypt_client_fn: CryptClientFn) -> Self {
        Fxfs { verbose: false, readonly: false, crypt_client_fn: Some(crypt_client_fn) }
    }

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

impl FSConfig for Fxfs {
    fn mode(&self) -> Mode<'_> {
        Mode::Component { name: "fxfs", reuse_component_after_serving: true }
    }
    fn crypt_client(&self) -> Option<zx::Channel> {
        self.crypt_client_fn.as_ref().map(|f| f())
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
    fn mode(&self) -> Mode<'_> {
        Mode::Legacy(LegacyConfig {
            binary_path: cstr!("/pkg/bin/factoryfs"),
            generic_args: {
                let mut args = vec![];
                if self.verbose {
                    args.push(cstr!("--verbose"));
                }
                args
            },
            ..Default::default()
        })
    }
}
