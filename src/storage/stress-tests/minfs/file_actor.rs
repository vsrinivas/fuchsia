// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_RIGHT_WRITABLE},
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    storage_stress_test_utils::{
        data::{Compressibility, FileData},
        io::Directory,
    },
    stress_test::actor::{Actor, ActorError},
};

#[derive(Debug)]
enum MinfsOperation {
    // Create [1, 1000] files that are reasonable in size ([8, 4096] KiB)
    CreateReasonableFiles,
}

// TODO(fxbug.dev/67497): This actor is very basic. At the moment, this is fine, since this is a
// v0 implementation of minfs stress tests. Eventually, we should write stress tests that exercise
// minfs as a true POSIX filesystem.
pub struct FileActor {
    rng: SmallRng,
    pub home_dir: Directory,
}

impl FileActor {
    pub fn new(rng: SmallRng, home_dir: Directory) -> Self {
        Self { rng, home_dir }
    }

    // Creates reasonable-sized files to fill a percentage of the free space
    // available on disk.
    async fn create_reasonable_files(&mut self) -> Result<(), Status> {
        let num_files_to_create: u64 = self.rng.gen_range(1, 1000);
        debug!("Creating {} files...", num_files_to_create);

        // Start filling the space with files
        for _ in 0..num_files_to_create {
            // Create a file whose size is reasonable, or exactly the requested size if the
            // requested size is too small.
            let filename = format!("file_{}", self.rng.gen::<u128>());

            let data =
                FileData::new_with_reasonable_size(&mut self.rng, Compressibility::Compressible);
            let bytes = data.generate_bytes();

            let file = self
                .home_dir
                .open_file(
                    &filename,
                    OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_WRITABLE,
                )
                .await?;
            file.write(&bytes).await?;
            file.close().await?;
        }

        Ok(())
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn valid_operations(&self) -> Vec<MinfsOperation> {
        vec![MinfsOperation::CreateReasonableFiles]
    }
}

#[async_trait]
impl Actor for FileActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let operations = self.valid_operations();
        let operation = operations.choose(&mut self.rng).unwrap();

        let result = match operation {
            MinfsOperation::CreateReasonableFiles => self.create_reasonable_files().await,
        };

        match result {
            Ok(()) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            Err(Status::PEER_CLOSED) => Err(ActorError::ResetEnvironment),
            Err(s) => panic!("Error occurred during {:?}: {}", operation, s),
        }
    }
}
