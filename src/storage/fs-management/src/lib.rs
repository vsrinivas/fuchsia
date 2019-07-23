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
//! generate_manifest("blobfs.manifest") {
//!   visibility = [ ":*" ]
//!   args = []
//!   foreach(pattern, [ "bin/blobfs" ]) {
//!     args += [ "--binary=" + pattern ]
//!   }
//! }
//! manifest_outputs = get_target_outputs(":blobfs.manifest")
//! manifest_file = manifest_outputs[0]
//! ...
//! package("foo") {
//!     extra = [ manifest_file ]
//!     deps = [
//!         ":blobfs.manifest",
//!         ....
//!     ]
//!     ...
//! }
//! ```
//!
//! This will put the blobfs command line utility into your sandbox at `/pkg/bin/blobfs` and allow
//! your component to launch it. This boilerplate will hopefully be reduced soon (ZX-4402)
//!
//! This library currently doesn't work outside of a component (`/pkg/bin/blobfs` is a hard-coded
//! string).
//!
//! This library doesn't currently support minfs, but support is planned (ZX-4302).

#![deny(missing_docs)]

use {
    cstr::cstr,
    failure::{bail, format_err, Error, ResultExt},
    fdio::{spawn_etc, Namespace, SpawnAction, SpawnOptions},
    fidl_fuchsia_io::{DirectoryAdminSynchronousProxy, OPEN_RIGHT_ADMIN},
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon::{self as zx, AsHandleRef},
};

/// A structure representing a managed blobfs partition. When dropped, it attempts to unmount blobfs
/// (if it was previously mounted).
pub struct Blobfs {
    namespace: Namespace,
    device_path: String,
    mount_point: Option<String>,
}

impl Drop for Blobfs {
    fn drop(&mut self) {
        if let Some(_) = self.mount_point {
            let _result = self.unmount();
        }
    }
}

impl Blobfs {
    /// Manage a blobfs partition on the provided device. The device is not formatted, mounted, or
    /// modified at this point.
    pub fn new(device_path: &str) -> Result<Self, Error> {
        let namespace = Namespace::installed().context("failed to get installed namespace")?;

        Ok(Blobfs { namespace, device_path: String::from(device_path), mount_point: None })
    }

    /// Format the associated device with a fresh blobfs. It must not be mounted.
    pub fn format(&self) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            // shouldn't be mounted if we are going to format it
            bail!("failed to format blobfs: mounted at {}", mount_point);
        }

        let (block_device, server_chan) =
            zx::Channel::create().context("failed to create channel")?;
        fdio::service_connect(&self.device_path, server_chan)
            .context("failed to connect to device (invalid path?)")?;
        let mut actions = vec![
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        // we want the underlying process to log to our stdout for now, so we let it clone it
        // (which, in this case, means SpawnOptions::all). in the future we may move towards what
        // fs-management does and log it to the kernel log instead.
        let options = SpawnOptions::CLONE_ALL;

        let argv = &[cstr!("/pkg/bin/blobfs"), cstr!("mkfs")];

        // if we pass handle::invalid to spawn it will use the default job
        let process = match spawn_etc(
            &zx::Handle::invalid().into(),
            options,
            argv[0],
            argv,
            None,
            &mut actions,
        ) {
            Ok(process) => process,
            Err((status, message)) => {
                bail!(
                    "failed to spawn blobfs format process: status: {}, message: {}",
                    status,
                    message
                );
            }
        };

        let _ = process
            .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
            .context("failed to run blobfs format")?;

        let info = process.info().context("failed to get process info from blobfs format run")?;
        if !info.exited || info.return_code != 0 {
            bail!("blobfs format failed ({})", info.return_code);
        }

        Ok(())
    }

    /// mount the provided block device as a blobfs partition at the path mount_point in the default
    /// namespace. Blobfs can't already be mounted, and the mount will fail if the provided mount
    /// path doesn't already exist. The path is relative to the root of the default namespace, and
    /// can't contain any '.' or '..' entries.
    pub fn mount(&mut self, mount_point: &str) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            // already mounted?
            bail!("failed to mount blobfs: already mounted at {}", mount_point);
        }

        let (block_device, server_chan) = zx::Channel::create()?;
        fdio::service_connect(&self.device_path, server_chan)?;
        let client_chan = Self::initialize(block_device)?;
        self.namespace
            .bind(mount_point, client_chan)
            .context("failed to bind blobfs client channel into default namespace")?;

        self.mount_point = Some(String::from(mount_point));

        Ok(())
    }

    /// Initialize the blobfs partition that exists on the provided block device, allowing it to
    /// recieve requests on the root channel. In order to be mounted in the traditional sense, the
    /// client side of the provided root channel needs to be bound to a path in a namespace
    /// somewhere.
    fn initialize(block_device: zx::Channel) -> Result<zx::Channel, Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;

        // create two spawn actions, one for each handle we have to pass to the blobfs binary
        let mut actions = vec![
            // root handle is passed in as a PA_USER0 handle at argument 0
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 0), server_chan.into()),
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        // we want the underlying process to log to our stdout for now, so we let it clone it
        // (which, in this case, means SpawnOptions::all). in the future we may move towards what
        // fs-management does and log it to the kernel log instead.
        let options = SpawnOptions::CLONE_ALL;

        let argv = &[cstr!("/pkg/bin/blobfs"), cstr!("--journal"), cstr!("mount")];

        // if we pass handle::invalid to spawn it will use the default job
        let _process = match spawn_etc(
            &zx::Handle::invalid().into(),
            options,
            argv[0],
            argv,
            None,
            &mut actions,
        ) {
            Ok(process) => process,
            Err((status, message)) => {
                // the second thing is some sort of error message.
                bail!("failed to spawn blobfs process: status: {}, message: {}", status, message);
            }
        };

        let signals = client_chan
            .wait_handle(zx::Signals::USER_0 | zx::Signals::CHANNEL_PEER_CLOSED, zx::Time::INFINITE)
            .context("failed to wait on root handle when mounting")?;

        if signals.contains(zx::Signals::CHANNEL_PEER_CLOSED) {
            bail!("failed to mount blobfs: CHANNEL_PEER_CLOSED");
        }

        Ok(client_chan)
    }

    /// Unmount the blobfs partition. The partition must already be mounted.
    pub fn unmount(&mut self) -> Result<(), Error> {
        let (client_chan, server_chan) = zx::Channel::create()?;
        let mount_point = self
            .mount_point
            .take()
            .ok_or_else(|| format_err!("failed to unmount blobfs: not mounted"))?;
        self.namespace
            .connect(&mount_point, OPEN_RIGHT_ADMIN, server_chan)
            .context("failed to connect to blobfs")?;

        let mut proxy = DirectoryAdminSynchronousProxy::new(client_chan);
        proxy.unmount(zx::Time::INFINITE).context("failed to unmount blobfs")?;

        self.namespace
            .unbind(&mount_point)
            .context("failed to unbind blobfs from default namespace")?;

        Ok(())
    }

    /// Run fsck on the blobfs partition. Returns Ok(()) if fsck succeeds, or the associated error
    /// if it doesn't. Will fail if run on a mounted partition.
    pub fn fsck(&self) -> Result<(), Error> {
        if let Some(mount_point) = &self.mount_point {
            bail!("failed to fsck blobfs: mounted at {}", mount_point);
        }

        let (block_device, server_chan) = zx::Channel::create()?;
        fdio::service_connect(&self.device_path, server_chan)?;
        let mut actions = vec![
            // device handle is passed in as a PA_USER0 handle at argument 1
            SpawnAction::add_handle(HandleInfo::new(HandleType::User0, 1), block_device.into()),
        ];

        // we want the underlying process to log to our stdout for now, so we let it clone it
        // (which, in this case, means SpawnOptions::all). in the future we may move towards what
        // fs-management does and log it to the kernel log instead.
        let options = SpawnOptions::CLONE_ALL;

        let argv = &[cstr!("/pkg/bin/blobfs"), cstr!("-j"), cstr!("fsck")];

        // if we pass handle::invalid to spawn it will use the default job
        let process = match spawn_etc(
            &zx::Handle::invalid().into(),
            options,
            argv[0],
            argv,
            None,
            &mut actions,
        ) {
            Ok(process) => process,
            Err((status, message)) => {
                // the second thing is some sort of error message.
                bail!("failed to spawn blobfs process: status: {}, message: {}", status, message);
            }
        };

        let _signals = process
            .wait_handle(zx::Signals::PROCESS_TERMINATED, zx::Time::INFINITE)
            .context("failed to wait for blobfs fsck process")?;

        let info = process.info().context("failed to get process info from blobfs fsck run")?;
        if !info.exited || info.return_code != 0 {
            bail!("blobfs fsck failed ({})", info.return_code);
        }

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::Blobfs,
        fuchsia_zircon::HandleBased,
        ramdevice_client::RamdiskClient,
        std::io::{Read, Write},
    };

    fn ramdisk_blobfs(block_size: u64) -> (RamdiskClient, Blobfs) {
        let ramdisk = RamdiskClient::create(block_size, 1 << 16).expect("failed to make ramdisk");
        let device_path = ramdisk.get_path();
        let blobfs = Blobfs::new(device_path).expect("failed to make new blobfs");
        (ramdisk, blobfs)
    }

    #[test]
    fn format_fsck_success() {
        let block_size = 512;
        let (ramdisk, blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");
        blobfs.fsck().expect("failed to fsck blobfs");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn format_fsck_fail() {
        let block_size = 512;
        let (ramdisk, blobfs) = ramdisk_blobfs(block_size);

        blobfs.format().expect("failed to format blobfs");

        // force fsck to fail by stomping all over one of blobfs's metadata blocks after formatting
        let device_channel = ramdisk.open().expect("failed to get channel to device");
        let mut file = fdio::create_fd(device_channel.into_handle())
            .expect("failed to convert to file descriptor");
        let mut bytes: Vec<u8> = std::iter::repeat(0xff).take(block_size as usize).collect();
        file.write(&mut bytes).expect("failed to write to device");

        blobfs.fsck().expect_err("fsck succeeded when it shouldn't have");

        ramdisk.destroy().expect("failed to destroy ramdisk");
    }

    #[test]
    fn format_mount_write_remount_read_unmount() {
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
}
