// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::actor::{Actor, ActorError},
};

/// An actor that destroys the ramdisk
pub struct InstanceActor {
    pub instance: Option<FvmInstance>,
}

impl InstanceActor {
    pub fn new(fvm: FvmInstance) -> Self {
        Self { instance: Some(fvm) }
    }
}

#[async_trait]
impl Actor for InstanceActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let fvm = self.instance.take();
        assert!(fvm.is_some());
        Err(ActorError::ResetEnvironment)
    }
}
