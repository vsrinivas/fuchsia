// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    storage_stress_test_utils::fvm::FvmInstance,
    stress_test::actor::{Actor, ActorError},
};

/// An actor that kills component manager, disconnecting the device from
/// the FVM driver.
pub struct InstanceActor {
    pub fvm: FvmInstance,
}

#[async_trait]
impl Actor for InstanceActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        self.fvm.kill_component_manager();
        Err(ActorError::ResetEnvironment)
    }
}
