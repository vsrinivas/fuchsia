// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        deletion_actor::DeletionActor, disconnect_actor::DisconnectActor, file_actor::FileActor,
        instance::MinfsInstance, Args,
    },
    async_trait::async_trait,
    fidl_fuchsia_hardware_block_partition::Guid,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fs_management::Minfs,
    fuchsia_zircon::Vmo,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    stress_test_utils::{
        actor::ActorConfig,
        environment::Environment,
        fvm::{get_volume_path, FvmInstance},
        io::Directory,
        random_seed,
    },
};

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

// The path to the minfs filesystem in the test's namespace
const MINFS_MOUNT_PATH: &str = "/minfs";

/// Describes the environment that this minfs stress test will run under.
#[derive(Debug)]
pub struct MinfsEnvironment {
    seed: u128,
    args: Args,
    vmo: Vmo,
    volume_guid: Guid,
}

impl MinfsEnvironment {
    pub async fn new(args: Args) -> Self {
        // Create the VMO that the ramdisk is backed by
        let vmo_size = args.ramdisk_block_count * args.ramdisk_block_size;
        let vmo = Vmo::create(vmo_size).unwrap();

        // Initialize the VMO with FVM partition style and a single minfs partition

        // Create a ramdisk and setup FVM.
        let mut fvm =
            FvmInstance::new(true, &vmo, args.fvm_slice_size, args.ramdisk_block_size).await;

        // Create a minfs volume
        let volume_guid = fvm.new_volume("minfs", TYPE_GUID).await;

        // Find the path to the volume
        let block_path = fvm.block_path();
        let volume_path = get_volume_path(block_path, &volume_guid).await;

        // Initialize minfs on volume
        let mut minfs = Minfs::new(volume_path.to_str().unwrap()).unwrap();
        minfs.format().unwrap();

        let seed = match args.seed {
            Some(seed) => seed,
            None => random_seed(),
        };

        Self { seed, args, vmo, volume_guid }
    }
}

#[async_trait]
impl Environment<MinfsInstance> for MinfsEnvironment {
    fn target_operations(&self) -> Option<u64> {
        self.args.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.args.time_limit_secs
    }

    async fn actors(&mut self) -> Vec<ActorConfig<MinfsInstance>> {
        let mut rng = SmallRng::from_seed(self.seed.to_le_bytes());

        let file_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            FileActor::new(rng, "home1".to_string())
        };
        let deletion_actor = {
            let rng = SmallRng::from_seed(rng.gen());
            DeletionActor::new(rng, "home1".to_string())
        };

        let mut actors = vec![
            ActorConfig::new("file_actor", file_actor, 0),
            ActorConfig::new("deletion_actor", deletion_actor, 5),
        ];

        if let Some(secs) = self.args.disconnect_secs {
            if secs > 0 {
                let actor = ActorConfig::new("disconnect_actor", DisconnectActor, secs);
                actors.push(actor);
            }
        }

        actors
    }

    async fn new_instance(&mut self) -> MinfsInstance {
        // Initialize the ramdisk and setup FVM.
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

        // Initialize minfs
        let mut minfs = Minfs::new(volume_path.to_str().unwrap()).unwrap();
        minfs.fsck().unwrap();
        minfs.mount(MINFS_MOUNT_PATH).unwrap();

        let root_dir =
            Directory::from_namespace(MINFS_MOUNT_PATH, OPEN_RIGHT_WRITABLE | OPEN_RIGHT_READABLE)
                .unwrap();

        MinfsInstance::new(minfs, fvm, root_dir)
    }
}
