// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::instance::MinfsInstance,
    async_trait::async_trait,
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    stress_test_utils::actor::{Actor, ActorError},
};

// An actor responsible for deleting files randomly
pub struct DeletionActor {
    rng: SmallRng,
    // Which directory to operate out of.
    home_dir: String,
}

impl DeletionActor {
    pub fn new(rng: SmallRng, home_dir: String) -> Self {
        Self { rng, home_dir }
    }
}

#[async_trait]
impl Actor<MinfsInstance> for DeletionActor {
    async fn perform(&mut self, instance: &mut MinfsInstance) -> Result<(), ActorError> {
        let home_dir = match instance.open_dir(&self.home_dir).await {
            Ok(d) => d,
            Err(Status::PEER_CLOSED) => {
                return Err(ActorError::GetNewInstance);
            }
            Err(s) => panic!("Unexpected error from open_dir: {}", s),
        };
        // Get list of all files
        let files = match home_dir.entries().await {
            Ok(files) => files,
            Err(Status::PEER_CLOSED) => return Err(ActorError::GetNewInstance),
            Err(s) => panic!("Error occurred during delete: {}", s),
        };

        if files.is_empty() {
            return Err(ActorError::DoNotCount);
        }

        let num_files_to_delete = self.rng.gen_range(1, files.len());
        debug!("Deleting {} files", num_files_to_delete);

        // Randomly select files from the list and remove them
        let files_to_delete = files.choose_multiple(&mut self.rng, num_files_to_delete);
        for file in files_to_delete {
            match home_dir.remove(file).await {
                Ok(()) => {}
                Err(Status::PEER_CLOSED) => return Err(ActorError::GetNewInstance),
                Err(s) => panic!("Error occurred during delete: {}", s),
            }
        }

        Ok(())
    }
}
