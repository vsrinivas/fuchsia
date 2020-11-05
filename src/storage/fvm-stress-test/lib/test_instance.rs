// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        fvm::{random_guid, Volume, VolumeManager, TYPE_GUID},
        state::VolumeOperator,
        utils::{create_ramdisk, init_fvm, start_fvm_driver, start_test, FVM_DRIVER_PATH},
    },
    fidl_fuchsia_device::ControllerProxy,
    fuchsia_async::Task,
    fuchsia_zircon::Vmo,
    futures::channel::mpsc,
    ramdevice_client::RamdiskClient,
    rand::{rngs::SmallRng, Rng, SeedableRng},
    std::path::PathBuf,
    test_utils_lib::opaque_test::OpaqueTest,
};

// The order of fields in this struct is important.
// Destruction happens top-down. Test must be destroyed last.
pub struct TestInstance {
    volume_manager: VolumeManager,
    controller: ControllerProxy,
    ramdisk: RamdiskClient,
    test: OpaqueTest,
}

impl TestInstance {
    // Create a test instance from the given VMO and initialize the ramdisk with FVM layout.
    pub async fn init(vmo: &Vmo, fvm_slice_size: usize, ramdisk_block_size: u64) -> Self {
        let test = start_test().await;
        let ramdisk = create_ramdisk(&test, vmo, ramdisk_block_size);

        let dev_path = test.get_hub_v2_path().join("exec/expose/dev");
        let ramdisk_path = dev_path.join(ramdisk.get_path());
        let ramdisk_path = ramdisk_path.to_str().unwrap();

        init_fvm(ramdisk_path, fvm_slice_size);
        let (controller, volume_manager) = start_fvm_driver(ramdisk_path).await;

        Self { test, controller, ramdisk, volume_manager }
    }

    pub fn crash(self) {
        // To crash the test, we do not want the ramdisk
        // to be destroyed cleanly. Forget the ramdisk struct.
        std::mem::forget(self.ramdisk);
    }

    pub async fn rebind(&mut self) {
        self.controller.rebind(FVM_DRIVER_PATH).await.unwrap().unwrap();
    }

    // Create a test instance from the given VMO. Assumes that the ramdisk already has
    // the FVM layout on it.
    pub async fn existing(vmo: &Vmo, ramdisk_block_size: u64) -> Self {
        let test = start_test().await;
        let ramdisk = create_ramdisk(&test, &vmo, ramdisk_block_size);

        let dev_path = test.get_hub_v2_path().join("exec/expose/dev");
        let ramdisk_path = dev_path.join(ramdisk.get_path());
        let ramdisk_path = ramdisk_path.to_str().unwrap();

        let (controller, volume_manager) = start_fvm_driver(ramdisk_path).await;

        Self { test, controller, ramdisk, volume_manager }
    }

    pub fn block_path(&self) -> PathBuf {
        self.test.get_hub_v2_path().join("exec/expose/dev/class/block")
    }

    pub async fn create_volumes_and_operators(
        &self,
        rng: &mut SmallRng,
        num_volumes: u64,
        max_slices_in_extend: u64,
        max_vslice_count: u64,
        num_operations: u64,
    ) -> (Vec<Task<()>>, Vec<mpsc::UnboundedSender<PathBuf>>) {
        let mut tasks = vec![];
        let mut senders = vec![];

        for i in 0..num_volumes {
            // Make a new RNG for this volume
            let volume_rng_seed: u128 = rng.gen();
            let mut volume_rng = SmallRng::from_seed(volume_rng_seed.to_le_bytes());

            let volume_name = format!("testpart-{}", i);
            let instance_guid = random_guid(&mut volume_rng);

            // Create the new volume
            self.volume_manager.new_volume(1, TYPE_GUID, instance_guid, &volume_name, 0x0).await;

            // Connect to the volume
            let (volume, sender) = Volume::new(self.block_path(), instance_guid).await;

            // Create the operator
            let operator = VolumeOperator::new(
                volume,
                volume_rng,
                max_slices_in_extend,
                max_vslice_count,
                num_operations,
            );

            // Start the operator
            let task = operator.run();

            tasks.push(task);
            senders.push(sender);
        }

        (tasks, senders)
    }
}
