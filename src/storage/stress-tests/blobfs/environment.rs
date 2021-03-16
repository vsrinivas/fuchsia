// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        blob_actor::BlobActor, deletion_actor::DeletionActor, instance_actor::InstanceActor, Args,
        BLOBFS_MOUNT_PATH,
    },
    async_trait::async_trait,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fs_management::Blobfs,
    fuchsia_zircon::Vmo,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::sync::Arc,
    storage_stress_test_utils::{
        fvm::{get_volume_path, FvmInstance},
        io::Directory,
    },
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

/// Describes the environment that this blobfs stress test will run under.
pub struct BlobfsEnvironment {
    seed: u128,
    args: Args,
    vmo: Vmo,
    volume_guid: Guid,
    blob_actor: Arc<Mutex<BlobActor>>,
    deletion_actor: Arc<Mutex<DeletionActor>>,
    instance_actor: Arc<Mutex<InstanceActor>>,
}

pub fn open_blobfs_root() -> Directory {
    Directory::from_namespace(BLOBFS_MOUNT_PATH, OPEN_RIGHT_WRITABLE | OPEN_RIGHT_READABLE).unwrap()
}

impl BlobfsEnvironment {
    pub async fn new(args: Args) -> Self {
        // Create the VMO that the ramdisk is backed by
        let vmo_size = args.ramdisk_block_count * args.ramdisk_block_size;
        let vmo = Vmo::create(vmo_size).unwrap();

        // Initialize the VMO with FVM partition style and a single blobfs partition

        // Create a ramdisk and setup FVM.
        let mut fvm =
            FvmInstance::new(true, &vmo, args.fvm_slice_size, args.ramdisk_block_size).await;

        // Create a blobfs volume
        let volume_guid = fvm.new_volume("blobfs", TYPE_GUID).await;

        // Find the path to the volume
        let block_path = fvm.block_path();
        let volume_path = get_volume_path(block_path, &volume_guid).await;

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
        let instance_actor =
            Arc::new(Mutex::new(InstanceActor { fvm, blobfs, instance_killed: false }));

        let mut rng = SmallRng::from_seed(seed.to_le_bytes());

        // Create the blob actor
        let blob_actor = Arc::new(Mutex::new(BlobActor {
            blobs: vec![],
            root_dir: open_blobfs_root(),
            rng: SmallRng::from_seed(rng.gen()),
        }));

        // Create the deletion actor
        let deletion_actor = Arc::new(Mutex::new(DeletionActor {
            root_dir: open_blobfs_root(),
            rng: SmallRng::from_seed(rng.gen()),
        }));

        Self { seed, args, vmo, volume_guid, instance_actor, blob_actor, deletion_actor }
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
            ActorRunner::new("blob_actor", 0, self.blob_actor.clone()),
            ActorRunner::new("deletion_actor", 10, self.deletion_actor.clone()),
        ];

        if let Some(secs) = self.args.disconnect_secs {
            if secs > 0 {
                let runner = ActorRunner::new("instance_actor", secs, self.instance_actor.clone());
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
            assert!(actor.instance_killed);

            // Kill the blobfs process in case it was still running
            let _ = actor.blobfs.kill();

            // Create a ramdisk and setup FVM.
            let fvm = FvmInstance::new(
                false,
                &self.vmo,
                self.args.fvm_slice_size,
                self.args.ramdisk_block_size,
            )
            .await;

            // Find the path to the volume
            let block_path = fvm.block_path();
            let volume_path = get_volume_path(block_path, &self.volume_guid).await;

            // Initialize blobfs on volume
            let mut blobfs = Blobfs::new(volume_path.to_str().unwrap()).unwrap();

            // Mount the blobfs volume
            blobfs.fsck().unwrap();
            blobfs.mount(BLOBFS_MOUNT_PATH).unwrap();

            // Replace the fvm and blobfs instances
            actor.fvm = fvm;
            actor.blobfs = blobfs;
            actor.instance_killed = false;
        }

        {
            // Replace the root directory with a new one
            let mut actor = self.blob_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }

        {
            // Replace the root directory with a new one
            let mut actor = self.deletion_actor.lock().await;
            actor.root_dir = open_blobfs_root();
        }
    }
}
