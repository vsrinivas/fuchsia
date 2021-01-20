// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::instance::BlobfsInstance,
    async_trait::async_trait,
    stress_test_utils::actor::{Actor, ActorError},
};

/// An actor that severs the connection between blobfs and the
/// underlying block device by killing component manager.
pub struct DisconnectActor;

#[async_trait]
impl Actor<BlobfsInstance> for DisconnectActor {
    async fn perform(&mut self, instance: &mut BlobfsInstance) -> Result<(), ActorError> {
        instance.kill_component_manager().await;
        Err(ActorError::GetNewInstance)
    }
}
