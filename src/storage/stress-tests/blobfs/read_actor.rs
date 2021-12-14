// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{SeekOrigin, OPEN_RIGHT_READABLE},
    fuchsia_zircon::Status,
    log::{debug, info},
    rand::{prelude::SliceRandom, rngs::SmallRng, Rng},
    storage_stress_test_utils::io::Directory,
    stress_test::actor::{Actor, ActorError},
};

// Performs operations on blobs expected to exist on disk
pub struct ReadActor {
    // Random number generator used by the operator
    pub rng: SmallRng,

    // Blobfs root directory
    pub root_dir: Directory,
}

impl ReadActor {
    pub fn new(rng: SmallRng, root_dir: Directory) -> Self {
        Self { rng, root_dir }
    }

    // Read a random amount of data at a random offset from a random blob
    async fn read_blob(&mut self) -> Result<(), Status> {
        // Decide how many blobs to create new handles for
        let blob_list = self.root_dir.entries().await?;

        if blob_list.is_empty() {
            // No blobs to read!
            return Ok(());
        }

        // Choose a random blob and open a handle to it
        let blob = blob_list.choose(&mut self.rng).unwrap();
        let file = self.root_dir.open_file(blob, OPEN_RIGHT_READABLE).await?;

        debug!("Reading from {}", blob);
        let data_size_bytes = file.uncompressed_size().await?;

        if data_size_bytes == 0 {
            // Nothing to read, blob is empty!
            return Ok(());
        }

        // Choose an offset
        let offset = self.rng.gen_range(0..data_size_bytes - 1);

        // Determine the length of this read
        let end_pos = self.rng.gen_range(offset..data_size_bytes);

        assert!(end_pos >= offset);
        let length = end_pos - offset;

        // Read the data from the handle and verify it
        file.seek(SeekOrigin::Start, offset).await?;
        let actual_data_bytes = file.read_num_bytes(length).await?;
        assert_eq!(actual_data_bytes.len(), length as usize);

        debug!("Read {} bytes from {}", length, blob);
        Ok(())
    }
}

#[async_trait]
impl Actor for ReadActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        match self.read_blob().await {
            Ok(()) => Ok(()),
            Err(Status::NOT_FOUND) => Ok(()),
            // Any other error is assumed to come from an intentional crash.
            // The environment verifies that an intentional crash occurred
            // and will panic if that is not the case.
            Err(s) => {
                info!("Read actor got status: {}", s);
                Err(ActorError::ResetEnvironment)
            }
        }
    }
}
