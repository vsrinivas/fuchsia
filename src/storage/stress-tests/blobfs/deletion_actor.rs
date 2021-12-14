// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_zircon::Status,
    log::{debug, info},
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    storage_stress_test_utils::io::Directory,
    stress_test::actor::{Actor, ActorError},
};

// An actor responsible for deleting blobs randomly
pub struct DeletionActor {
    pub rng: SmallRng,
    pub root_dir: Directory,
}

impl DeletionActor {
    pub fn new(rng: SmallRng, root_dir: Directory) -> Self {
        Self { rng, root_dir }
    }
}

#[async_trait]
impl Actor for DeletionActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        // Get list of all blobs
        let blobs = match self.root_dir.entries().await {
            Ok(blobs) => blobs,
            Err(Status::PEER_CLOSED) | Err(Status::CONNECTION_ABORTED) => {
                return Err(ActorError::ResetEnvironment)
            }
            Err(s) => panic!("Error occurred during delete: {}", s),
        };

        if blobs.is_empty() {
            return Err(ActorError::DoNotCount);
        }

        let num_blobs_to_delete = self.rng.gen_range(0..blobs.len());
        debug!("Deleting {} blobs", num_blobs_to_delete);

        // Randomly select blobs from the list and remove them
        let blobs_to_delete = blobs.choose_multiple(&mut self.rng, num_blobs_to_delete);
        for blob in blobs_to_delete {
            match self.root_dir.remove(blob).await {
                Ok(()) => {}
                // Any error is assumed to come from an intentional crash.
                // The environment verifies that an intentional crash occurred
                // and will panic if that is not the case.
                Err(s) => {
                    info!("Deletion actor got status: {}", s);
                    return Err(ActorError::ResetEnvironment);
                }
            }
        }

        Ok(())
    }
}
