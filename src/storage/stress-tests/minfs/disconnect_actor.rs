// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::instance::MinfsInstance,
    async_trait::async_trait,
    stress_test_utils::actor::{Actor, ActorError},
};

/// An actor that severs the connection between minfs and the
/// underlying block device by killing component manager.
pub struct DisconnectActor;

#[async_trait]
impl Actor<MinfsInstance> for DisconnectActor {
    async fn perform(&mut self, instance: &mut MinfsInstance) -> Result<(), ActorError> {
        instance.kill_component_manager().await;
        Err(ActorError::GetNewInstance)
    }
}
