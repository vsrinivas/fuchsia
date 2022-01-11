// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_device::ControllerMarker,
    fidl_fuchsia_io::{DirectoryProxy, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fs_management::{Blobfs, Filesystem},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon::{AsHandleRef, Rights, Vmo},
    io_util::{file::read, open_directory_in_namespace, open_file_in_namespace},
    ramdevice_client::{wait_for_device, RamdiskClient, VmoRamdiskClientBuilder},
    std::{convert::TryInto, fs::OpenOptions, path::PathBuf, time::Duration},
    storage_isolated_driver_manager::bind_fvm,
};

// The block size to use for the ramdisk backing FVM+blobfs.
const RAMDISK_BLOCK_SIZE: u64 = 512;

// The subdirectory from the FVM topological path to the blobfs block device
// topological path. Full path may be, for example,
// `/dev/sys/platform/00:00:2d/ramctl/ramdisk-0/block/fvm/{FVM_BLOBFS_BOCK_SUBDIR}`.
//
// TODO(fxbug.dev/88614): FVM+blobfs client code such as this would be less
// brittle if the there was a library for waiting on this block device.
const FVM_BLOBFS_BOCK_SUBDIR: &str = "blobfs-p-1/block";

/// Wrapper around `fs_management::Filesystem<Blobfs>` that retains objects for
/// instantiating blobfs loaded from an in-memory copy of an FVM image.
pub struct BlobfsInstance {
    _fvm_vmo: Vmo,
    _fvm: FvmInstance,
    blobfs: Filesystem<Blobfs>,
    blobfs_dir: Option<String>,
}

impl BlobfsInstance {
    /// Instantiate blobfs using fvm block file store at `fvm_resource_path`.
    pub async fn new_from_resource(fvm_resource_path: &str) -> Self {
        // Create a VMO filled with the FVM image stored at `fvm_resource_path`.
        let fvm_file = open_file_in_namespace(fvm_resource_path, OPEN_RIGHT_READABLE).unwrap();
        let fvm_buf = read(&fvm_file).await.unwrap();
        let fvm_size = fvm_buf.len();
        let fvm_vmo = Vmo::create(fvm_size.try_into().unwrap()).unwrap();
        fvm_vmo.write(&fvm_buf, 0).unwrap();

        // Create a ramdisk; do not init FVM (init=false) as we are loading an
        // existing image.
        let fvm = FvmInstance::new(&fvm_vmo, RAMDISK_BLOCK_SIZE).await;

        let blobfs_block_path = format!("{}/{}", fvm.topological_path(), FVM_BLOBFS_BOCK_SUBDIR);

        // Wait for device at blobfs block path before interacting with it.
        wait_for_device(&blobfs_block_path, Duration::from_secs(20)).unwrap();

        // Instantiate blobfs.
        let mut blobfs = Blobfs::new(&blobfs_block_path).unwrap();

        // Check blobfs consistency.
        blobfs.fsck().unwrap();

        Self { _fvm_vmo: fvm_vmo, _fvm: fvm, blobfs, blobfs_dir: None }
    }

    /// Mount blobfs to directory location `mountpoint`.
    pub fn mount(&mut self, mountpoint: &str) {
        if let Some(blobfs_dir) = &self.blobfs_dir {
            panic!(
                "Attempt to re-mount blobfs at {} when it is already mounted at {}",
                mountpoint, blobfs_dir
            );
        }

        self.blobfs.mount(mountpoint).unwrap();
        self.blobfs_dir = Some(mountpoint.to_string());
    }

    /// Umount blobfs.
    pub fn unmount(&mut self) {
        if self.blobfs_dir.is_some() {
            self.blobfs.unmount().unwrap();
            self.blobfs_dir = None;
            return;
        }

        panic!("Attempt to unmount blobfs when it is not mounted");
    }

    /// Open the blobfs root directory.
    pub fn open_root_dir(&self) -> DirectoryProxy {
        if let Some(blobfs_dir) = &self.blobfs_dir {
            return open_directory_in_namespace(
                blobfs_dir,
                OPEN_RIGHT_WRITABLE | OPEN_RIGHT_READABLE,
            )
            .unwrap();
        }

        panic!("Attempt to open blobfs root when it is not mounted");
    }
}

//
// Forked from
// https://fuchsia.googlesource.com/fuchsia/+/30e9e1cc3b019ef1d7aabc620b470f8a6518db55/src/storage/stress-tests/utils/fvm.rs
//

// These are the paths associated with the driver_test_realm client shard:
// //src/storage/testing/driver_test_realm/meta/client.shard.cml
// and the isolated devmgr device topology.
pub const DEV_PATH: &'static str = "/dev";
pub const RAMCTL_PATH: &'static str = "/dev/sys/platform/00:00:2d/ramctl";

fn wait_for_ramctl() {
    wait_for_device(RAMCTL_PATH, Duration::from_secs(20))
        .expect("Could not wait for ramctl from isolated-devmgr");
}

fn create_ramdisk(vmo: &Vmo, ramdisk_block_size: u64) -> RamdiskClient {
    let duplicated_handle = vmo.as_handle_ref().duplicate(Rights::SAME_RIGHTS).unwrap();
    let duplicated_vmo = Vmo::from(duplicated_handle);

    // Create the ramdisks
    let dev_root = OpenOptions::new().read(true).write(true).open(&DEV_PATH).unwrap();
    VmoRamdiskClientBuilder::new(duplicated_vmo)
        .block_size(ramdisk_block_size)
        .dev_root(dev_root)
        .build()
        .unwrap()
}

async fn start_fvm_driver(ramdisk_path: &str) -> String {
    let controller = connect_to_protocol_at_path::<ControllerMarker>(ramdisk_path).unwrap();
    bind_fvm(&controller).await.unwrap();

    // Wait until the FVM driver is available
    let fvm_path = PathBuf::from(ramdisk_path).join("fvm");

    let fvm_path = fvm_path.to_str().unwrap();
    wait_for_device(fvm_path, Duration::from_secs(20)).unwrap();
    String::from(fvm_path)
}

/// This structs holds processes of component manager, isolated-devmgr
/// and the fvm driver.
///
/// NOTE: The order of fields in this struct is important.
/// Destruction happens top-down. Test must be destroyed last.
pub struct FvmInstance {
    /// Manages the ramdisk device that is backed by a VMO
    _ramdisk: RamdiskClient,

    /// Topological path to FVM on ramdisk.
    topological_path: String,
}

impl FvmInstance {
    /// Start an isolated FVM driver against the given VMO.
    /// If `init` is true, initialize the VMO with FVM layout first.
    pub async fn new(vmo: &Vmo, ramdisk_block_size: u64) -> Self {
        wait_for_ramctl();
        let ramdisk = create_ramdisk(&vmo, ramdisk_block_size);

        let dev_path = PathBuf::from(DEV_PATH);
        let ramdisk_path = dev_path.join(ramdisk.get_path());
        let ramdisk_path = ramdisk_path.to_str().unwrap();

        let topological_path = start_fvm_driver(ramdisk_path).await;

        Self { _ramdisk: ramdisk, topological_path }
    }

    pub fn topological_path<'a>(&'a self) -> &'a str {
        &self.topological_path
    }
}
