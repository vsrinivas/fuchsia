// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fuchsia_zircon::Status,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    storage_stress_test_utils::io::Directory,
    stress_test::actor::{Actor, ActorError},
    tracing::{debug, info},
};

// An actor responsible for deleting files randomly
pub struct DeletionActor {
    rng: SmallRng,
    pub home_dir: Directory,
}

impl DeletionActor {
    pub fn new(rng: SmallRng, home_dir: Directory) -> Self {
        Self { rng, home_dir }
    }
}

#[async_trait]
impl Actor for DeletionActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        // Get list of all files
        let files = match self.home_dir.entries().await {
            Ok(files) => files,
            Err(Status::PEER_CLOSED) => return Err(ActorError::ResetEnvironment),
            // TODO(fxbug.dev/113953): This should only be accepted after the instance actor has
            // stopped the block device.
            Err(Status::CANCELED) => return Err(ActorError::ResetEnvironment),
            Err(s) => panic!("Error occurred during delete: {}", s),
        };

        if files.is_empty() {
            return Err(ActorError::DoNotCount);
        }

        let num_files_to_delete = self.rng.gen_range(0..files.len());
        debug!("Deleting {} files", num_files_to_delete);

        // Randomly select files from the list and remove them
        let files_to_delete = files.choose_multiple(&mut self.rng, num_files_to_delete);
        for file in files_to_delete {
            let res = self.home_dir.remove(file).await;
            match &res {
                Ok(()) => {}
                // Unlink should never fail because of insufficient resources, since it frees
                // resources.  This is especially important to verify for journaling and
                // log-structured filesystems like Fxfs.
                Err(Status::NO_SPACE) | Err(Status::NO_RESOURCES) | Err(Status::NO_MEMORY) => {
                    panic!("Unlink failed with {:?}, but should always succeed.", res)
                }
                // Any other error is assumed to come from an intentional crash.
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
