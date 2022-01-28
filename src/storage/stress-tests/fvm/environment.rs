// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        instance_actor::InstanceActor, volume::VolumeConnection, volume_actor::VolumeActor, Args,
    },
    async_trait::async_trait,
    fidl_fuchsia_hardware_block_partition::Guid,
    fuchsia_zircon::Vmo,
    futures::lock::Mutex,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::sync::Arc,
    std::time::Duration,
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::{actor::ActorRunner, environment::Environment, random_seed},
};

// All partitions in this test have their type set to this arbitrary GUID.
const TYPE_GUID: Guid = Guid {
    value: [0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf],
};

/// Describes the environment that this blobfs stress test will run under.
pub struct FvmEnvironment {
    seed: u64,
    args: Args,
    vmo: Vmo,
    instance_actor: Arc<Mutex<InstanceActor>>,
    volume_actors: Vec<(Guid, Arc<Mutex<VolumeActor>>)>,
}

impl FvmEnvironment {
    pub async fn new(args: Args) -> Self {
        // Create the VMO that the ramdisk is backed by
        let vmo_size = args.ramdisk_block_count * args.ramdisk_block_size;
        let vmo = Vmo::create(vmo_size).unwrap();

        // Create a ramdisk and setup FVM.
        let mut fvm =
            FvmInstance::new(true, &vmo, args.fvm_slice_size, args.ramdisk_block_size).await;

        // Create the root RNG
        let seed = match args.seed {
            Some(seed) => seed,
            None => random_seed(),
        };
        let mut rng = SmallRng::seed_from_u64(seed);

        let mut volume_actors = vec![];
        for i in 0..args.num_volumes {
            // Create the new volume
            let volume_name = format!("testpart-{}", i);
            let volume_guid = fvm.new_volume(&volume_name, TYPE_GUID, 1).await;

            // Connect to the volume
            let volume = VolumeConnection::new(volume_guid, args.fvm_slice_size).await;

            // Create the actor
            let rng = SmallRng::from_seed(rng.gen());
            let volume_actor = Arc::new(Mutex::new(
                VolumeActor::new(volume, rng, args.max_slices_in_extend, args.max_vslice_count)
                    .await,
            ));

            volume_actors.push((volume_guid, volume_actor));
        }

        let instance_actor = Arc::new(Mutex::new(InstanceActor::new(fvm)));

        Self { seed, args, vmo, instance_actor, volume_actors }
    }
}

impl std::fmt::Debug for FvmEnvironment {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Environment").field("seed", &self.seed).field("args", &self.args).finish()
    }
}

#[async_trait]
impl Environment for FvmEnvironment {
    fn target_operations(&self) -> Option<u64> {
        self.args.num_operations
    }

    fn timeout_seconds(&self) -> Option<u64> {
        self.args.time_limit_secs
    }

    fn actor_runners(&mut self) -> Vec<ActorRunner> {
        let mut runners = vec![];

        for (guid, actor) in &self.volume_actors {
            let actor_name = format!("volume_actor_{}", guid.value[0]);
            runners.push(ActorRunner::new(actor_name, None, actor.clone()));
        }

        if let Some(secs) = self.args.disconnect_secs {
            if secs > 0 {
                runners.push(ActorRunner::new(
                    "instance_actor",
                    Some(Duration::from_secs(secs)),
                    self.instance_actor.clone(),
                ))
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

            // Start isolated-devmgr and FVM
            let fvm = FvmInstance::new(
                false,
                &self.vmo,
                self.args.fvm_slice_size,
                self.args.ramdisk_block_size,
            )
            .await;

            // Replace the FVM instance
            actor.instance = Some(fvm);
        }

        for (guid, actor) in &self.volume_actors {
            let mut actor = actor.lock().await;

            // Connect to the volume
            let volume = VolumeConnection::new(*guid, self.args.fvm_slice_size).await;

            // Replace the volume
            actor.volume = volume;
        }
    }
}
