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
//! generate_manifest("fs.manifest") {
//!   visibility = [ ":*" ]
//!   args = [ "--binary=bin/blobfs", "--binary=bin/minfs" ]
//! }
//! manifest_outputs = get_target_outputs(":fs.manifest")
//! manifest_file = manifest_outputs[0]
//! ...
//! package("foo") {
//!     extra = [ manifest_file ]
//!     deps = [
//!         ":fs.manifest",
//!         ....
//!     ]
//!     ...
//! }
//! ```
//!
//! This will put the filesystem utility binaries into your sandbox in `/pkg/bin` and allow
//! your component to launch it. This boilerplate will hopefully be reduced soon (ZX-4402)
//!
//! This library currently doesn't work outside of a component (the filesystem utility binary paths
//! are hard-coded strings).

#![deny(missing_docs)]

use {
    cstr::cstr,
    failure::{bail, format_err, Error, ResultExt},
    fdio::{spawn_etc, Namespace, SpawnAction, SpawnOptions},
    fidl_fuchsia_io::{DirectoryAdminSynchronousProxy, OPEN_RIGHT_ADMIN},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, AsHandleRef},
    std::{ffi::CStr, marker::PhantomData},
};

/// Describes the information for working with a particular native filesystem.
pub trait Layout {
    /// Path to the filesystem utility binary.
    fn path() -> &'static CStr;
    /// A human readable name for the filesystem.
    fn name() -> &'static str;
}

/// Filesystem represents a managed filesystem partition with a particular layout. It is constructed
/// with functions associated with the [`Layout`] types. Right now, those include [`Blobfs`] and
/// [`Minfs`].
pub struct Filesystem<FSType>
where
    FSType: Layout,
{
    namespace: Namespace,
    device_path: String,
    mount_point: Option<String>,
    _marker: PhantomData<FSType>,
}

impl<FSType> Drop for Filesystem<FSType>
where
    FSType: Layout,
{
    fn drop(&mut self) {
        if let Some(_) = self.mount_point {
            let _result = self.unmount();
        }
    }
}

/// The blobfs layout type.
pub struct Blobfs;

impl Blobfs {
    /// Manage a blobfs partition on the provided device. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn new(device_path: &str) -> Result<Filesystem<Self>, Error> {
        Filesystem::new(device_path)
    }
}

impl Layout for Blobfs {
    fn path() -> &'static CStr {
        cstr!("/pkg/bin/blobfs")
    }

    fn name() -> &'static str {
        "blobfs"
    }
}

/// The minfs layout type.
pub struct Minfs;

impl Minfs {
    /// Manage a minfs partition on the provided device. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn new(device_path: &str) -> Result<Filesystem<Self>, Error> {
        Filesystem::new(device_path)
    }
}

impl Layout for Minfs {
    fn path() -> &'static CStr {
        cstr!("/pkg/bin/minfs")
    }

    fn name() -> &'static str {
        "minfs"
    }
}

impl<FSType> Filesystem<FSType>
where
    FSType: Layout,
{
    /// Manage a filesystem partition on the provided device. The device is not formatted, mounted,
    /// or modified at this point.
    ///
    /// This function is not public. The only way to construct a new filesystem type is through one
    /// of the structs that implements Layout.
    fn new(device_path: &str) -> Result<Filesystem<FSType>, Error> {
        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        Ok(Filesystem {
            namespace,
            device_path: String::from(device_path),
            mount_point: None,
            _marker: PhantomData,
        })
    }

    /// Initialize the filesystem partition that exists on the provided block device, allowing it to
    /// recieve requests on the root channel. In order to be mounted in the traditional sense, the
    /// client side of the provided root channel needs to be bound to a path in a namespace
    /// somewhere.
    fn initialize(block_device: zx::Channel) -> Result<zx::Channel, Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        let actions = vec![
            // root handle is passed in as a PA_USER0 handle at argument 0
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 0), server_chan.into()),
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        let args = &[FSType::path(), cstr!("mount")];

        let _process = launch_process(args, actions).context("failed to mount")?;

        let signals = client_chan
            .wait_handle(zx::Signals::USER_0 | zx::Signals::CHANNEL_PEER_CLOSED, zx::Time::INFINITE)
            .context("failed to wait on root handle when mounting")?;

        if signals.contains(zx::Signals::CHANNEL_PEER_CLOSED) {
            bail!("failed to mount: CHANNEL_PEER_CLOSED");
        }

        Ok(client_chan)
    }

    /// Format the associated device with a fresh filesystem. It must not be mounted.
    pub fn format(&self) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            // shouldn't be mounted if we are going to format it
            bail!("failed to format {}: mounted at {}", FSType::name(), mount_point);
        }

        let args = &[FSType::path(), cstr!("mkfs")];

        run_process_on_device(args, &self.device_path).context("failed to format device")?;

        Ok(())
    }

    /// Mount the provided block device and bind it to the provided mount_point in the default
    /// namespace. The filesystem can't already be mounted, and the mount will fail if the provided
    /// mount path doesn't already exist. The path is relative to the root of the default namespace,
    /// and can't contain any '.' or '..' entries.
    pub fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            // already mounted?
            bail!("failed to mount {}: already mounted at {}", FSType::name(), mount_point);
        }

        let (block_device, server_chan) = zx::Channel::create()?;
        fdio::service_connect(&self.device_path, server_chan)?;
        let client_chan = Self::initialize(block_device)?;
        self.namespace
            .bind(mount_point, client_chan)
            .context("failed to bind client channel into default namespace")?;

        self.mount_point = Some(String::from(mount_point));

        Ok(())
    }

    /// Unmount the filesystem partition. The partition must already be mounted.
    pub fn unmount(&mut self) -> Result<(), Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;
        let mount_point =
            self.mount_point.take().ok_or_else(|| format_err!("failed to unmount: not mounted"))?;
        self.namespace
            .connect(&mount_point, OPEN_RIGHT_ADMIN, server_chan)
            .context("failed to connect to filesystem")?;

        let mut proxy = DirectoryAdminSynchronousProxy::new(client_chan);
        proxy.unmount(zx::Time::INFINITE).context("failed to unmount")?;

        self.namespace
            .unbind(&mount_point)
            .context("failed to unbind filesystem from default namespace")?;

        Ok(())
    }

    /// Run fsck on the filesystem partition. Returns Ok(()) if fsck succeeds, or the associated
    /// error if it doesn't. Will fail if run on a mounted partition.
    pub fn fsck(&self) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            bail!("failed to fsck: mounted at {}", mount_point);
        }

        let args = &[FSType::path(), cstr!("fsck")];

        run_process_on_device(args, &self.device_path).context("fsck failed")?;

        Ok(())
    }
}

/// run process to completion, passing a handle to the device at device_path as a PA_USER0 handle at
/// argument 1. non-zero return codes are transformed into an error.
fn run_process_on_device(args: &[&CStr], device_path: &str) -> Result<(), Error> {
    let (block_device, server_chan) = zx::Channel::create()?;
    fdio::service_connect(device_path, server_chan)
        .context(format!("failed to connect to device at {} (wrong path?)", device_path))?;
    let actions = vec![
        // device handle is passed in as a PA_USER0 handle at argument 1
        SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
    ];

    let process = launch_process(args, actions)?;

    let _signals = process
        .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
        .context(format!("failed to wait for process to complete. launched with {:?}", args))?;

    let info = process.info().context("failed to get process info")?;
    if !info.exited || info.return_code != 0 {
        bail!(
            "process returned non-zero exit code ({}). launched with {:?}",
            info.return_code,
            args
        );
    }

    Ok(())
}

/// launch a new process, returning the process object.
fn launch_process(args: &[&CStr], mut actions: Vec<SpawnAction>) -> Result<zx::Process, Error> {
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
            bail!(
                "failed to spawn process. launched with: {:?}, status: {}, message: {}",
                args,
                status,
                message
            );
        }
    };

    Ok(process)
}

#[cfg(test)]
mod tests {
    use {
        super::{Blobfs, Filesystem, Minfs},
        fuchsia_zircon::HandleBased,
        ramdevice_client::RamdiskClient,
        std::io::{Read, Seek, Write},
    };

    fn ramdisk_blobfs(block_size: u64) -> (RamdiskClient, Filesystem<Blobfs>) {
        let ramdisk = RamdiskClient::create(block_size, 1 << 16).expect("failed to make ramdisk");
        let device_path = ramdisk.get_path();
        let blobfs = Blobfs::new(device_path).expect("failed to make new blobfs");
        (ramdisk, blobfs)
    }

    #[test]
    fn blobfs_format_fsck_success() {
        let block_size = 512;
        let (ramdisk, blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");
        blobfs.fsck().expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_format_fsck_fail() {
        let block_size = 512;
        let (ramdisk, blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");

        // force fsck to fail by stomping all over one of blobfs's metadata blocks after formatting
        // TODO(35860): corrupt something other than the superblock
        let device_channel = ramdisk.open().expect("failed to get channel to device");
        let mut file = fdio::create_fd(device_channel.into_handle())
            .expect("failed to convert to file descriptor");
        let mut bytes: Vec<u8> = std::iter::repeat(0xff).take(block_size as usize).collect();
        file.write(&mut bytes).expect("failed to write to device");

        blobfs.fsck().expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_format_mount_write_remount_read_unmount() {
        let block_size = 512;
        let mount_point = "/test-fs-root";
        let (ramdisk, mut blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");
        blobfs.mount(mount_point).expect("failed to mount blobfs");

        // pre-generated merkle test fixture data
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let content = String::from("test content").into_bytes();
        let path = format!("{}/{}", mount_point, merkle);

        {
            let mut test_file = std::fs::File::create(&path).expect("failed to create test file");
            test_file.set_len(content.len() as u64).expect("failed to truncate file");
            test_file.write(&content).expect("failed to write to test file");
        }

        blobfs.unmount().expect("failed to unmount blobfs");
        blobfs.mount(mount_point).expect("failed to mount blobfs");

        {
            let mut test_file = std::fs::File::open(&path).expect("failed to open test file");
            let mut read_content = Vec::new();
            test_file.read_to_end(&mut read_content).expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        blobfs.unmount().expect("failed to unmount blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    fn ramdisk_minfs(block_size: u64) -> (RamdiskClient, Filesystem<Minfs>) {
        let ramdisk = RamdiskClient::create(block_size, 1 << 16).expect("failed to make ramdisk");
        let device_path = ramdisk.get_path();
        let minfs = Minfs::new(device_path).expect("failed to make new minfs");
        (ramdisk, minfs)
    }

    #[test]
    fn minfs_format_fsck_success() {
        let block_size = 8192;
        let (ramdisk, minfs) = ramdisk_minfs(block_size);

        minfs.format().expect("failed to format minfs");
        minfs.fsck().expect("failed to fsck minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn minfs_format_fsck_fail() {
        let block_size = 8192;
        let (ramdisk, minfs) = ramdisk_minfs(block_size);

        minfs.format().expect("failed to format minfs");

        // force fsck to fail by stomping all over one of minfs's metadata blocks after formatting
        let device_channel = ramdisk.open().expect("failed to get channel to device");
        let mut file = fdio::create_fd(device_channel.into_handle())
            .expect("failed to convert to file descriptor");

        // when minfs isn't on an fvm, the location for it's bitmap offset is the 8th block.
        // TODO(35861): parse the superblock for this offset and the block size.
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
    fn minfs_format_mount_write_remount_read_unmount() {
        let block_size = 8192;
        let mount_point = "/test-fs-root";
        let (ramdisk, mut minfs) = ramdisk_minfs(block_size);

        minfs.format().expect("failed to format minfs");
        minfs.mount(mount_point).expect("failed to mount minfs");

        let filename = "test_file";
        let content = String::from("test content").into_bytes();
        let path = format!("{}/{}", mount_point, filename);

        {
            let mut test_file = std::fs::File::create(&path).expect("failed to create test file");
            test_file.write(&content).expect("failed to write to test file");
        }

        minfs.unmount().expect("failed to unmount minfs");
        minfs.mount(mount_point).expect("failed to mount minfs");

        {
            let mut test_file = std::fs::File::open(&path).expect("failed to open test file");
            let mut read_content = Vec::new();
            test_file.read_to_end(&mut read_content).expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        minfs.unmount().expect("failed to unmount minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }
}
