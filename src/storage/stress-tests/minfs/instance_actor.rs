// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fs_management::{Filesystem, Minfs},
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::actor::{Actor, ActorError},
};

/// An actor that severs the connection between minfs and the
/// underlying block device by killing component manager.
pub struct InstanceActor {
    pub fvm: FvmInstance,
    pub minfs: Filesystem<Minfs>,
}

#[async_trait]
impl Actor for InstanceActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        self.fvm.kill_component_manager();
        Err(ActorError::ResetEnvironment)
    }
}
