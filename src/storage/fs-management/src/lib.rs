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
    fdio::{
        clone_channel, create_fd, spawn_etc, transfer_fd, Namespace, SpawnAction, SpawnOptions,
    },
    fidl_fuchsia_io::{DirectoryAdminSynchronousProxy, OPEN_RIGHT_ADMIN},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, AsHandleRef},
    std::{ffi::CStr, marker::PhantomData},
};

/// Options to pass to the underlying filesystem process. They are passed as argument flags, and are
/// always present on the call even if they don't apply to the operation.
#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub struct FSOptions {
    /// Filesystem will be mounted read-only. Applies to mounting.
    pub readonly: bool,
    /// Increase logging output from the filesystem. Applies to mounting, fsck, and format.
    pub verbose: bool,
    /// Configure metric collection by the filesystem. Applies to mounting.
    pub metrics: bool,
    /// Enable journaling in the filesystem. Applies to mounting and fsck.
    pub journal: bool,
}

/// Describes the information for working with a particular native filesystem.
pub trait Layout {
    /// Path to the filesystem utility binary.
    fn path() -> &'static CStr;
    /// A human readable name for the filesystem.
    fn name() -> &'static str;
    /// Default options for the binary for this filesystem layout.
    fn options() -> FSOptions;
}

/// Filesystem represents a managed filesystem partition with a particular layout. It is constructed
/// with functions associated with the [`Layout`] types. Right now, those include [`Blobfs`] and
/// [`Minfs`].
pub struct Filesystem<FSType>
where
    FSType: Layout,
{
    namespace: Namespace,
    device: Option<zx::Channel>,
    mount_point: Option<String>,
    launcher: FSLauncher<FSType, ProcLauncher>,
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
        let (block_device, server_chan) = zx::Channel::create()?;
        fdio::service_connect(device_path, server_chan).context("connecting to block device")?;
        Filesystem::new(block_device)
    }

    /// Manage a blobfs partition on the provided device. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn from_channel(device_channel: zx::Channel) -> Result<Filesystem<Self>, Error> {
        Filesystem::new(device_channel)
    }
}

impl Layout for Blobfs {
    fn path() -> &'static CStr {
        cstr!("/pkg/bin/blobfs")
    }

    fn name() -> &'static str {
        "blobfs"
    }

    fn options() -> FSOptions {
        FSOptions { readonly: false, verbose: false, metrics: false, journal: true }
    }
}

/// The minfs layout type.
pub struct Minfs;

impl Minfs {
    /// Manage a minfs partition on the provided device. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn new(device_path: &str) -> Result<Filesystem<Self>, Error> {
        let (block_device, server_chan) = zx::Channel::create()?;
        fdio::service_connect(device_path, server_chan).context("connecting to block device")?;
        Filesystem::new(block_device)
    }

    /// Manage a minfs partition on the provided device. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn from_channel(device_channel: zx::Channel) -> Result<Filesystem<Self>, Error> {
        Filesystem::new(device_channel)
    }
}

impl Layout for Minfs {
    fn path() -> &'static CStr {
        cstr!("/pkg/bin/minfs")
    }

    fn name() -> &'static str {
        "minfs"
    }

    fn options() -> FSOptions {
        FSOptions { readonly: false, verbose: false, metrics: false, journal: true }
    }
}

impl Filesystem<Minfs> {
    /// Increase logging output from the filesystem process. Only the minfs binary has this option.
    pub fn set_verbose(&mut self, enable: bool) {
        self.launcher.options.verbose = enable;
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
    fn new(device: zx::Channel) -> Result<Filesystem<FSType>, Error> {
        let namespace = Namespace::installed().context("failed to get installed namespace")?;
        Ok(Filesystem {
            namespace,
            device: Some(device),
            mount_point: None,
            launcher: FSLauncher::new(FSType::options()),
        })
    }

    /// Configure journal support.
    pub fn set_journal(&mut self, enable: bool) {
        self.launcher.options.journal = enable;
    }

    /// Mount the filesystem as read only.
    pub fn set_readonly(&mut self, enable: bool) {
        self.launcher.options.readonly = enable;
    }

    /// Configure the collection of metrics.
    pub fn set_metrics(&mut self, enable: bool) {
        self.launcher.options.metrics = enable;
    }

    /// Initialize the filesystem partition that exists on the provided block device, allowing it to
    /// receive requests on the root channel. In order to be mounted in the traditional sense, the
    /// client side of the provided root channel needs to be bound to a path in a namespace
    /// somewhere.
    fn initialize(&self, block_device: zx::Channel) -> Result<zx::Channel, Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        let actions = vec![
            // root handle is passed in as a PA_USER0 handle at argument 0
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 0), server_chan.into()),
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        let _process =
            self.launcher.run_command(cstr!("mount"), actions).context("failed to mount")?;

        let signals = client_chan
            .wait_handle(zx::Signals::USER_0 | zx::Signals::CHANNEL_PEER_CLOSED, zx::Time::INFINITE)
            .context("failed to wait on root handle when mounting")?;

        if signals.contains(zx::Signals::CHANNEL_PEER_CLOSED) {
            bail!("failed to mount: CHANNEL_PEER_CLOSED");
        }

        Ok(client_chan)
    }

    /// Returns a channel to the block device.
    fn get_channel(&mut self) -> Result<zx::Channel, Error> {
        // Create a channel clone.
        let file = create_fd(self.device.take().unwrap().into())?;
        let channel = clone_channel(&file).context("Clonning device channel")?;
        self.device = Some((transfer_fd(file)?).into());

        Ok(channel)
    }

    /// Format the associated device with a fresh filesystem. It must not be mounted.
    pub fn format(&mut self) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            // shouldn't be mounted if we are going to format it
            bail!("failed to format {}: mounted at {}", FSType::name(), mount_point);
        }

        let device = self.get_channel()?;

        self.launcher
            .run_command_with_device(cstr!("mkfs"), device)
            .context("failed to format device")?;

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

        let channel = self.get_channel()?;
        let client_chan = self.initialize(channel)?;
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
    pub fn fsck(&mut self) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            bail!("failed to fsck: mounted at {}", mount_point);
        }

        let device = self.get_channel()?;

        self.launcher.run_command_with_device(cstr!("fsck"), device).context("fsck failed")?;

        Ok(())
    }
}

trait Launcher {
    fn launch_process(args: &[&CStr], actions: Vec<SpawnAction<'_>>) -> Result<zx::Process, Error>;
}

struct ProcLauncher;
impl Launcher for ProcLauncher {
    fn launch_process(
        args: &[&CStr],
        mut actions: Vec<SpawnAction<'_>>,
    ) -> Result<zx::Process, Error> {
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
}

struct FSLauncher<FSType, L>
where
    FSType: Layout,
    L: Launcher,
{
    pub options: FSOptions,
    _type_marker: PhantomData<FSType>,
    _launcher_marker: PhantomData<L>,
}

impl<FSType, L> FSLauncher<FSType, L>
where
    FSType: Layout,
    L: Launcher,
{
    pub fn new(options: FSOptions) -> Self {
        FSLauncher { options, _type_marker: PhantomData, _launcher_marker: PhantomData }
    }

    pub fn run_command_with_device(
        &self,
        command: &'static CStr,
        block_device: zx::Channel,
    ) -> Result<(), Error> {
        let actions = vec![
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        let process = self.run_command(command, actions)?;

        let _signals = process
            .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
            .context(format!("failed to wait for process to complete"))?;

        let info = process.info().context("failed to get process info")?;
        if !info.exited || info.return_code != 0 {
            bail!("process returned non-zero exit code ({})", info.return_code);
        }

        Ok(())
    }

    pub fn run_command(
        &self,
        command: &'static CStr,
        actions: Vec<SpawnAction<'_>>,
    ) -> Result<zx::Process, Error> {
        let mut args = vec![FSType::path()];
        if self.options.journal {
            args.push(cstr!("--journal"));
        }
        if self.options.metrics {
            args.push(cstr!("--metrics"));
        }
        if self.options.readonly {
            args.push(cstr!("--readonly"));
        }
        if self.options.verbose {
            args.push(cstr!("--verbose"));
        }
        args.push(command);

        L::launch_process(&args, actions)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{Blobfs, FSLauncher, FSOptions, Filesystem, Launcher, Minfs},
        cstr::cstr,
        failure::Error,
        fdio::SpawnAction,
        fuchsia_zircon::{self as zx, HandleBased},
        ramdevice_client::RamdiskClient,
        std::ffi::CStr,
        std::io::{Read, Seek, Write},
    };

    /// the only way to really move info out of the launch_process function is through the return
    /// value. we want to confirm that the correct one was called, so we return something only a
    /// test impl can return - something defined in the test mod - through the error type.
    #[derive(Debug, Copy, Clone, PartialEq, Eq)]
    struct ExpectedError;
    impl std::fmt::Display for ExpectedError {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            write!(f, "test passed")
        }
    }
    impl failure::Fail for ExpectedError {}

    #[test]
    fn fs_launcher_no_args() {
        struct TestLauncherNoArgs;
        impl Launcher for TestLauncherNoArgs {
            fn launch_process(
                args: &[&CStr],
                _actions: Vec<SpawnAction<'_>>,
            ) -> Result<zx::Process, Error> {
                assert_eq!(args, &[cstr!("/pkg/bin/blobfs"), cstr!("mount")]);
                Err(ExpectedError.into())
            }
        }

        let launcher: FSLauncher<Blobfs, TestLauncherNoArgs> = FSLauncher::new(FSOptions {
            readonly: false,
            journal: false,
            verbose: false,
            metrics: false,
        });
        let res = launcher.run_command(cstr!("mount"), vec![]);
        assert_eq!(
            res.unwrap_err().as_fail().downcast_ref::<ExpectedError>().unwrap(),
            &ExpectedError
        );
    }

    #[test]
    fn fs_launcher_all_args() {
        struct TestLauncherAllArgs;
        impl Launcher for TestLauncherAllArgs {
            fn launch_process(
                args: &[&CStr],
                _actions: Vec<SpawnAction<'_>>,
            ) -> Result<zx::Process, Error> {
                assert_eq!(
                    args,
                    &[
                        cstr!("/pkg/bin/blobfs"),
                        cstr!("--journal"),
                        cstr!("--metrics"),
                        cstr!("--readonly"),
                        cstr!("--verbose"),
                        cstr!("mount")
                    ]
                );
                Err(ExpectedError.into())
            }
        }

        let launcher: FSLauncher<Blobfs, TestLauncherAllArgs> = FSLauncher::new(FSOptions {
            readonly: true,
            journal: true,
            verbose: true,
            metrics: true,
        });
        let res = launcher.run_command(cstr!("mount"), vec![]);
        assert_eq!(
            res.unwrap_err().as_fail().downcast_ref::<ExpectedError>().unwrap(),
            &ExpectedError
        );
    }

    fn ramdisk_blobfs(block_size: u64) -> (RamdiskClient, Filesystem<Blobfs>) {
        let ramdisk = RamdiskClient::create(block_size, 1 << 16).expect("failed to make ramdisk");
        let device_path = ramdisk.get_path();
        let blobfs = Blobfs::new(device_path).expect("failed to make new blobfs");
        (ramdisk, blobfs)
    }

    #[test]
    fn blobfs_format_fsck_success() {
        let block_size = 512;
        let (ramdisk, mut blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");
        blobfs.fsck().expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_from_channel_format_fsck_success() {
        let block_size = 512;
        let (ramdisk, _) = ramdisk_blobfs(block_size);

        let (block_device, server_chan) = zx::Channel::create().expect("failed to create channel");
        fdio::service_connect(ramdisk.get_path(), server_chan).expect("failed to connect");

        let mut blobfs = Blobfs::from_channel(block_device).expect("failed to make new blobfs");

        blobfs.format().expect("failed to format blobfs");
        blobfs.fsck().expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn blobfs_format_fsck_fail() {
        let block_size = 512;
        let (ramdisk, mut blobfs) = ramdisk_blobfs(block_size);

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

    // #[test]
    #[allow(dead_code)] // TODO(36120): test is flakey. Re-enable when flake is tracked down.
    fn blobfs_format_mount_write_remount_read_unmount() {
        let block_size = 512;
        let mount_point = "/test-fs-root";
        let (ramdisk, mut blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");
        blobfs.mount(mount_point).expect("failed to mount blobfs the first time");

        // pre-generated merkle test fixture data
        let merkle = "be901a14ec42ee0a8ee220eb119294cdd40d26d573139ee3d51e4430e7d08c28";
        let content = String::from("test content").into_bytes();
        let path = format!("{}/{}", mount_point, merkle);

        {
            let mut test_file = std::fs::File::create(&path).expect("failed to create test file");
            test_file.set_len(content.len() as u64).expect("failed to truncate file");
            test_file.write(&content).expect("failed to write to test file");
        }

        blobfs.unmount().expect("failed to unmount blobfs the first time");
        blobfs.mount(mount_point).expect("failed to mount blobfs the second time");

        {
            let mut test_file = std::fs::File::open(&path).expect("failed to open test file");
            let mut read_content = Vec::new();
            test_file.read_to_end(&mut read_content).expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        blobfs.unmount().expect("failed to unmount blobfs the second time");

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
        let (ramdisk, mut minfs) = ramdisk_minfs(block_size);

        minfs.format().expect("failed to format minfs");
        minfs.fsck().expect("failed to fsck minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn minfs_from_channel_format_fsck_success() {
        let block_size = 8192;
        let (ramdisk, _) = ramdisk_minfs(block_size);

        let (block_device, server_chan) = zx::Channel::create().expect("failed to create channel");
        fdio::service_connect(ramdisk.get_path(), server_chan).expect("failed to connect");

        let mut minfs = Minfs::from_channel(block_device).expect("failed to make new minfs");

        minfs.format().expect("failed to format minfs");
        minfs.fsck().expect("failed to fsck minfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn minfs_format_fsck_fail() {
        let block_size = 8192;
        let (ramdisk, mut minfs) = ramdisk_minfs(block_size);

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

    // #[test]
    #[allow(dead_code)] // TODO(36120): test is flakey. Re-enable when flake is tracked down.
    fn minfs_format_mount_write_remount_read_unmount() {
        let block_size = 8192;
        let mount_point = "/test-fs-root";
        let (ramdisk, mut minfs) = ramdisk_minfs(block_size);

        minfs.format().expect("failed to format minfs");
        minfs.mount(mount_point).expect("failed to mount minfs the first time");

        let filename = "test_file";
        let content = String::from("test content").into_bytes();
        let path = format!("{}/{}", mount_point, filename);

        {
            let mut test_file = std::fs::File::create(&path).expect("failed to create test file");
            test_file.write(&content).expect("failed to write to test file");
        }

        minfs.unmount().expect("failed to unmount minfs the first time");
        minfs.mount(mount_point).expect("failed to mount minfs the second time");

        {
            let mut test_file = std::fs::File::open(&path).expect("failed to open test file");
            let mut read_content = Vec::new();
            test_file.read_to_end(&mut read_content).expect("failed to read from test file");
            assert_eq!(content, read_content);
        }

        minfs.unmount().expect("failed to unmount minfs the second time");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }
}
