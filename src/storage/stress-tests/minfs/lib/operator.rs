// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::MINFS_MOUNT_PATH,
    fidl_fuchsia_io::{
        OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    stress_test_utils::{
        data::{Compressibility, FileDataFactory},
        io::Directory,
    },
};

// TODO(xbhatnag): This operator is very basic. At the moment, this is fine, since this is a
// v0 implementation of minfs stress tests. Eventually, we should write stress tests that exercise
// minfs as a true POSIX filesystem.

#[derive(Debug)]
enum MinfsOperation {
    CreateReasonableFiles,
    DeleteAllFiles,
}

// In-memory state of minfs. Stores information about files expected to exist on disk.
pub struct MinfsOperator {
    // The path to the minfs root
    root_dir: Directory,

    // Random number generator used for selecting operations
    rng: SmallRng,

    // Factory for creating file data that meets certain specifications
    factory: FileDataFactory,
}

async fn wait_for_minfs_dir() -> Directory {
    loop {
        match Directory::from_namespace(MINFS_MOUNT_PATH, OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE)
        {
            Ok(dir) => break dir,
            Err(Status::NOT_FOUND) => continue,
            Err(Status::PEER_CLOSED) => continue,
            Err(s) => panic!("Unexpected error opening minfs dir: {}", s),
        }
    }
}

impl MinfsOperator {
    pub async fn new(mut initial_rng: SmallRng) -> Self {
        // Setup the RNGs
        let root_dir = wait_for_minfs_dir().await;
        let factory_rng = SmallRng::from_seed(initial_rng.gen());
        let state_rng = SmallRng::from_seed(initial_rng.gen());

        let factory = FileDataFactory::new(factory_rng);

        Self { root_dir, rng: state_rng, factory }
    }

    // Creates reasonable-sized files to fill a percentage of the free space
    // available on disk.
    async fn create_reasonable_files(&mut self) -> Result<(), Status> {
        let num_files_to_create: u64 = self.rng.gen_range(1, 1000);
        debug!("Creating {} files...", num_files_to_create);

        // Start filling the space with files
        for _ in 0..num_files_to_create {
            // Create a file whose uncompressed size is reasonable, or exactly the requested size
            // if the requested size is too small.
            let filename = format!("file_{}", self.rng.gen::<u128>());

            let data = self.factory.create_with_reasonable_size(Compressibility::Compressible);
            let bytes = data.generate_bytes();

            let file = self
                .root_dir
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

    // Removes all files from the filesystem at random
    async fn delete_all_files(&mut self) -> Result<(), Status> {
        let mut entries = self.root_dir.entries().await?;
        entries.shuffle(&mut self.rng);
        for entry in entries {
            self.root_dir.remove(&entry).await?;
        }
        Ok(())
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn get_operation_list(&self) -> Vec<MinfsOperation> {
        // It is always valid to do the following operations
        vec![MinfsOperation::CreateReasonableFiles, MinfsOperation::DeleteAllFiles]
    }

    async fn do_operation(&mut self, operation: &MinfsOperation) -> Result<(), Status> {
        match operation {
            MinfsOperation::CreateReasonableFiles => self.create_reasonable_files().await,
            MinfsOperation::DeleteAllFiles => self.delete_all_files().await,
        }
    }

    pub async fn do_random_operations(mut self, num_operations: u64) {
        for index in 1..=num_operations {
            let operations = self.get_operation_list();
            debug!("Operations = {:?}", operations);
            let operation = operations.choose(&mut self.rng).unwrap();

            debug!("{} ---->>>> {:?}", index, operation);
            let result = self.do_operation(operation).await;

            if let Err(Status::PEER_CLOSED) = result {
                // Reconnect to the minfs mount point
                self.root_dir = wait_for_minfs_dir().await;
            }

            debug!("{} <<<<---- {:?} [{:?}]", index, operation, result);
        }
    }
}
