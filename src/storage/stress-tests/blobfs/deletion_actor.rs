// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::instance::BlobfsInstance,
    async_trait::async_trait,
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    stress_test_utils::actor::{Actor, ActorError},
};

// An actor responsible for deleting blobs randomly
pub struct DeletionActor {
    rng: SmallRng,
}

impl DeletionActor {
    pub fn new(rng: SmallRng) -> Self {
        Self { rng }
    }
}

#[async_trait]
impl Actor<BlobfsInstance> for DeletionActor {
    async fn perform(&mut self, instance: &mut BlobfsInstance) -> Result<(), ActorError> {
        // Get list of all blobs
        let blobs = match instance.root_dir.entries().await {
            Ok(blobs) => blobs,
            Err(Status::PEER_CLOSED) | Err(Status::CONNECTION_ABORTED) => {
                return Err(ActorError::GetNewInstance)
            }
            Err(s) => panic!("Error occurred during delete: {}", s),
        };

        if blobs.is_empty() {
            return Err(ActorError::DoNotCount);
        }

        let num_blobs_to_delete = self.rng.gen_range(0, blobs.len());
        debug!("Deleting {} blobs", num_blobs_to_delete);

        // Randomly select blobs from the list and remove them
        let blobs_to_delete = blobs.choose_multiple(&mut self.rng, num_blobs_to_delete);
        for blob in blobs_to_delete {
            match instance.root_dir.remove(blob).await {
                Ok(()) => {}
                Err(Status::PEER_CLOSED) | Err(Status::CONNECTION_ABORTED) => {
                    return Err(ActorError::GetNewInstance)
                }
                Err(s) => panic!("Error occurred during delete: {}", s),
            }
        }

        Ok(())
    }
}
