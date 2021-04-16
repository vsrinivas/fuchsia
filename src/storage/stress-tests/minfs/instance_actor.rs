// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fs_management::{Filesystem, Minfs},
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::actor::{Actor, ActorError},
};

/// An actor that kills minfs and destroys the ramdisk
pub struct InstanceActor {
    pub instance: Option<(Filesystem<Minfs>, FvmInstance)>,
}

impl InstanceActor {
    pub fn new(fvm: FvmInstance, minfs: Filesystem<Minfs>) -> Self {
        Self { instance: Some((minfs, fvm)) }
    }
}

#[async_trait]
impl Actor for InstanceActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        if let Some((mut minfs, _)) = self.instance.take() {
            minfs.kill().expect("Could not kill minfs");
        } else {
            panic!("Instance was already killed!")
        }
        Err(ActorError::ResetEnvironment)
    }
}
