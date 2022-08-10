// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    either::Either,
    fs_management::filesystem::{ServingMultiVolumeFilesystem, ServingSingleVolumeFilesystem},
    fuchsia_async as fasync,
    std::time::Duration,
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::actor::{Actor, ActorError},
};

/// An actor that kills the fs instance and destroys the ramdisk
pub struct InstanceActor {
    pub instance:
        Option<(FvmInstance, Either<ServingSingleVolumeFilesystem, ServingMultiVolumeFilesystem>)>,
}

impl InstanceActor {
    pub fn new(
        fvm: FvmInstance,
        fs: Either<ServingSingleVolumeFilesystem, ServingMultiVolumeFilesystem>,
    ) -> Self {
        Self { instance: Some((fvm, fs)) }
    }
}

#[async_trait]
impl Actor for InstanceActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        if let Some((fvm_instance, fs)) = self.instance.take() {
            // We want to kill the ram-disk before the filesystem so that we test the filesystem in
            // a simulated power-fail.
            let path = fvm_instance.ramdisk_path();
            std::mem::drop(fvm_instance);
            // Wait for the device to go away.
            let mut count = 0;
            while let Ok(_) = std::fs::metadata(&path) {
                count += 1;
                assert!(count < 100);
                fasync::Timer::new(Duration::from_millis(100)).await;
            }
            match fs {
                Either::Left(fs) => fs.kill().await.expect("Could not kill fs instance"),
                // TODO(fxbug.dev/105888): Make termination more abrupt.
                Either::Right(fs) => fs.shutdown().await.expect("Could not kill fs instance"),
            };
        } else {
            panic!("Instance was already killed!")
        }
        Err(ActorError::ResetEnvironment)
    }
}
