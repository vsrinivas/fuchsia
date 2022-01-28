// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        blob_actor::BlobActor, deletion_actor::DeletionActor, instance_actor::InstanceActor,
        read_actor::ReadActor, Args, BLOBFS_MOUNT_PATH,
    },
    async_trait::async_trait,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fs_management::Blobfs,
    fuchsia_zircon::Vmo,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::sync::Arc,
    std::time::Duration,
    storage_stress_test_utils::{
        data::{Compressibility, FileFactory, UncompressedSize},
        fvm::{get_volume_path, FvmInstance},
        io::Directory,
    },
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

const EIGHT_KIB: u64 = 8192;
const ONE_MIB: u64 = 1048576;
const FOUR_MIB: u64 = 4 * ONE_MIB;

/// Describes the environment that this blobfs stress test will run under.
pub struct BlobfsEnvironment {
    seed: u64,
    args: Args,
    vmo: Vmo,
    volume_guid: Guid,
    small_blob_actor: Arc<Mutex<BlobActor>>,
    medium_blob_actor: Arc<Mutex<BlobActor>>,
    large_blob_actor: Arc<Mutex<BlobActor>>,
    deletion_actor: Arc<Mutex<DeletionActor>>,
    instance_actor: Arc<Mutex<InstanceActor>>,
    read_actor: Arc<Mutex<ReadActor>>,
}

pub fn open_blobfs_root() -> Directory {
    Directory::from_namespace(BLOBFS_MOUNT_PATH, OPEN_RIGHT_WRITABLE | OPEN_RIGHT_READABLE).unwrap()
}

impl BlobfsEnvironment {
    pub async fn new(args: Args) -> Self {
        // Create the VMO that the ramdisk is backed by
        let disk_size = args.ramdisk_block_count * args.ramdisk_block_size;
        let vmo = Vmo::create(disk_size).unwrap();

        // Initialize the VMO with FVM partition style and a single blobfs partition

        // Create a ramdisk and setup FVM.
        let mut fvm =
            FvmInstance::new(true, &vmo, args.fvm_slice_size, args.ramdisk_block_size).await;

        // Create a blobfs volume
        let volume_guid = fvm.new_volume("blobfs", TYPE_GUID, 1).await;

        // Find the path to the volume
        let volume_path = get_volume_path(&volume_guid).await;

        // Initialize blobfs on volume
        let mut blobfs = Blobfs::new(volume_path.to_str().unwrap()).unwrap();
        blobfs.format().unwrap();

        let seed = match args.seed {
            Some(seed) => seed,
            None => random_seed(),
        };

        // Mount the blobfs volume
        blobfs.fsck().unwrap();
        blobfs.mount(BLOBFS_MOUNT_PATH).unwrap();

        // Create the instance actor
        let instance_actor = Arc::new(Mutex::new(InstanceActor::new(fvm, blobfs)));

        let mut rng = SmallRng::seed_from_u64(seed);

        // Create the blob actors
        let small_blob_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let uncompressed_size = UncompressedSize::Exact(EIGHT_KIB);
            let compressibility = Compressibility::Uncompressible;
            let factory = FileFactory::new(rng, uncompressed_size, compressibility);
            let root_dir = open_blobfs_root();
            Arc::new(Mutex::new(BlobActor::new(factory, root_dir)))
        };

        let medium_blob_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let uncompressed_size = UncompressedSize::InRange(ONE_MIB, FOUR_MIB);
            let compressibility = Compressibility::Compressible;
            let factory = FileFactory::new(rng, uncompressed_size, compressibility);
            let root_dir = open_blobfs_root();
            Arc::new(Mutex::new(BlobActor::new(factory, root_dir)))
        };
        let large_blob_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let uncompressed_size = UncompressedSize::Exact(2 * disk_size);
            let compressibility = Compressibility::Compressible;
            let factory = FileFactory::new(rng, uncompressed_size, compressibility);
            let root_dir = open_blobfs_root();
            Arc::new(Mutex::new(BlobActor::new(factory, root_dir)))
        };

        // Create the read actor
        let read_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let root_dir = open_blobfs_root();
            Arc::new(Mutex::new(ReadActor::new(rng, root_dir)))
        };

        // Create the deletion actor
        let deletion_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            let root_dir = open_blobfs_root();
            Arc::new(Mutex::new(DeletionActor::new(rng, root_dir)))
        };

        Self {
            seed,
            args,
            vmo,
            volume_guid,
            instance_actor,
            small_blob_actor,
            medium_blob_actor,
            large_blob_actor,
            read_actor,
            deletion_actor,
        }
    }
}

impl std::fmt::Debug for BlobfsEnvironment {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Environment").field("seed", &self.seed).field("args", &self.args).finish()
    }
}

#[async_trait]
impl Environment for BlobfsEnvironment {
    fn target_operations(&self) -> Option<u64> {
        self.args.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.args.time_limit_secs
    }

    fn actor_runners(&mut self) -> Vec<ActorRunner> {
        let mut runners = vec![
            ActorRunner::new("small_blob_actor", None, self.small_blob_actor.clone()),
            ActorRunner::new("medium_blob_actor", None, self.medium_blob_actor.clone()),
            ActorRunner::new("large_blob_actor", None, self.large_blob_actor.clone()),
            ActorRunner::new("read_actor", None, self.read_actor.clone()),
            ActorRunner::new(
                "deletion_actor",
                Some(Duration::from_secs(10)),
                self.deletion_actor.clone(),
            ),
        ];

        if let Some(secs) = self.args.disconnect_secs {
            if secs > 0 {
                let runner = ActorRunner::new(
                    "instance_actor",
                    Some(Duration::from_secs(secs)),
                    self.instance_actor.clone(),
                );
                runners.push(runner);
            }
        }

        runners
    }

    async fn reset(&mut self) {
        {
            let mut actor = self.instance_actor.lock().await;

            // The environment is only reset when the instance is killed.
            // TODO(72385): Pass the actor error here, so it can be printed out on assert failure.
            assert!(actor.instance.is_none());

            // Create a ramdisk and setup FVM.
            let fvm = FvmInstance::new(
                false,
                &self.vmo,
                self.args.fvm_slice_size,
                self.args.ramdisk_block_size,
            )
            .await;

            // Find the path to the volume
            let volume_path = get_volume_path(&self.volume_guid).await;

            // Initialize blobfs on volume
            let mut blobfs = Blobfs::new(volume_path.to_str().unwrap()).unwrap();

            // Mount the blobfs volume
            blobfs.fsck().unwrap();
            blobfs.mount(BLOBFS_MOUNT_PATH).unwrap();

            // Replace the fvm and blobfs instances
            actor.instance = Some((blobfs, fvm));
        }

        // Replace the root directory with a new one
        {
            let mut actor = self.small_blob_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }

        {
            let mut actor = self.medium_blob_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }

        {
            let mut actor = self.large_blob_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }

        {
            let mut actor = self.read_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }

        {
            let mut actor = self.deletion_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }
    }
}
