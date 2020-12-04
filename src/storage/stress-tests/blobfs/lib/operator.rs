// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blob::Blob,
    crate::BLOBFS_MOUNT_PATH,
    fidl_fuchsia_io::{SeekOrigin, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_zircon::Status,
    log::{debug, warn},
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    stress_test_utils::{
        data::{Compressibility, FileDataFactory},
        io::Directory,
    },
};

const BLOCK_SIZE: u64 = fuchsia_merkle::BLOCK_SIZE as u64;

#[derive(Debug)]
enum BlobfsOperation {
    CreateReasonableBlobs,
    DeleteSomeBlobs,
    FillDiskWithSmallBlobs,
    DeleteAllBlobs,
    NewHandles,
    CloseAllHandles,
    ReadFromAllHandles,
    VerifyBlobs,
}

// In-memory state of blobfs. Stores information about blobs expected to exist on disk.
pub struct BlobfsOperator {
    // The path to the blobfs root
    root_dir: Directory,

    // In-memory representations of all blobs as they exist on disk
    blobs: Vec<Blob>,

    // Random number generator used for selecting operations and blobs
    rng: SmallRng,

    // Factory for creating blob data that meets certain specifications
    factory: FileDataFactory,
}

async fn wait_for_blobfs_dir() -> Directory {
    loop {
        match Directory::from_namespace(
            BLOBFS_MOUNT_PATH,
            OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
        ) {
            Ok(dir) => break dir,
            Err(Status::NOT_FOUND) => continue,
            Err(Status::PEER_CLOSED) => continue,
            Err(s) => panic!("Unexpected error opening blobfs dir: {}", s),
        }
    }
}

impl BlobfsOperator {
    pub async fn new(mut initial_rng: SmallRng) -> Self {
        // Setup the RNGs
        let root_dir = wait_for_blobfs_dir().await;
        let factory_rng = SmallRng::from_seed(initial_rng.gen());
        let state_rng = SmallRng::from_seed(initial_rng.gen());

        let factory = FileDataFactory::new(factory_rng);

        Self { root_dir, blobs: vec![], rng: state_rng, factory }
    }

    // Deletes blob at [index] from the filesystem and from the list of known blobs.
    async fn delete_blob(&mut self, index: usize) -> Result<Blob, Status> {
        let merkle_root_hash = self.blobs.get(index).unwrap().merkle_root_hash();
        match self.root_dir.remove(merkle_root_hash).await {
            Ok(()) => {}
            Err(Status::NOT_FOUND) => {
                warn!(
                    "Blob {} does not exist. Possibly lost during fault injection?",
                    merkle_root_hash
                );
            }
            Err(s) => {
                return Err(s);
            }
        }
        Ok(self.blobs.remove(index))
    }

    fn hashes(&self) -> Vec<String> {
        self.blobs.iter().map(|b| b.merkle_root_hash().to_string()).collect()
    }

    // Reads in all blobs stored on the filesystem and compares them to our in-memory
    // model to ensure that everything is as expected.
    async fn verify_blobs(&self) -> Result<(), Status> {
        let on_disk_hashes = self.root_dir.entries().await?.sort_unstable();
        let in_memory_hashes = self.hashes().sort_unstable();

        assert_eq!(on_disk_hashes, in_memory_hashes);

        for blob in &self.blobs {
            match blob.verify_from_disk(&self.root_dir).await {
                Ok(()) => {}
                Err(Status::NOT_FOUND) => {
                    warn!(
                        "Blob {} does not exist. Possibly lost during fault injection?",
                        blob.merkle_root_hash()
                    );
                    continue;
                }
                Err(s) => {
                    return Err(s);
                }
            }
        }

        Ok(())
    }

    // Creates reasonable-sized blobs to fill a percentage of the free space
    // available on disk.
    async fn create_reasonable_blobs(&mut self) -> Result<(), Status> {
        let num_blobs_to_create: u64 = self.rng.gen_range(1, 200);
        debug!("Creating {} blobs...", num_blobs_to_create);

        // Start filling the space with blobs
        for _ in 0..num_blobs_to_create {
            // Create a blob whose uncompressed size is reasonable, or exactly the requested size
            // if the requested size is too small.
            let data = self.factory.create_with_reasonable_size(Compressibility::Compressible);
            let blob = Blob::create(data, &self.root_dir).await?;
            self.blobs.push(blob);
        }

        Ok(())
    }

    // Deletes a random number of blobs from the disk
    async fn delete_some_blobs(&mut self) -> Result<(), Status> {
        // Decide how many blobs to delete
        let num_blobs_to_delete = self.rng.gen_range(0, self.num_blobs());
        debug!("Deleting {} blobs", num_blobs_to_delete);

        // Randomly select blobs from the list and remove them
        for _ in 0..num_blobs_to_delete {
            let index = self.rng.gen_range(0, self.num_blobs());
            self.delete_blob(index).await?;
        }

        Ok(())
    }

    // Creates open handles for a random number of blobs
    async fn new_handles(&mut self) -> Result<(), Status> {
        // Decide how many blobs to create new handles for
        let num_blobs_with_new_handles = self.rng.gen_range(0, self.num_blobs());
        debug!("Creating handles for {} blobs", num_blobs_with_new_handles);

        // Randomly select blobs from the list and create handles to them
        for _ in 0..num_blobs_with_new_handles {
            // Choose a random blob and open a handle to it
            let blob = self.blobs.choose_mut(&mut self.rng).unwrap();
            match self.root_dir.open_file(blob.merkle_root_hash(), OPEN_RIGHT_READABLE).await {
                Ok(handle) => blob.handles().push(handle),
                Err(Status::NOT_FOUND) => {
                    warn!(
                        "Blob {} does not exist. Possibly lost during fault injection?",
                        blob.merkle_root_hash()
                    );
                }
                Err(s) => {
                    return Err(s);
                }
            }
        }

        Ok(())
    }

    // Closes all open handles to all blobs.
    // Note that handles do not call `close()` when they are dropped.
    // This is intentional because it offers a way to inelegantly close a file.
    async fn close_all_handles(&mut self) -> Result<(), Status> {
        let mut count = 0;
        for blob in &mut self.blobs {
            for handle in blob.handles().drain(..) {
                handle.close().await?;
                count += 1;
            }
        }
        debug!("Closed {} handles to blobs", count);
        Ok(())
    }

    // Reads random portions of a blob from all open handles
    async fn read_from_all_handles(&mut self) -> Result<(), Status> {
        let mut count: u64 = 0;
        let mut total_bytes_read: u64 = 0;
        for blob in &mut self.blobs {
            let data_size_bytes = blob.data().size_bytes;
            let data_bytes = blob.data().generate_bytes();

            for handle in blob.handles() {
                // Choose an offset (0 if the blob is empty)
                let offset =
                    if data_size_bytes == 0 { 0 } else { self.rng.gen_range(0, data_size_bytes) };

                // Determine the length of this read (0 if the blob is empty)
                let end_pos = if data_size_bytes == 0 {
                    0
                } else {
                    self.rng.gen_range(offset, data_size_bytes)
                };

                assert!(end_pos >= offset);
                let length = end_pos - offset;

                // Read the data from the handle and verify it
                handle.seek(SeekOrigin::Start, offset).await?;
                let actual_data_bytes = handle.read_num_bytes(length).await?;
                let expected_data_bytes = &data_bytes[offset as usize..end_pos as usize];
                assert_eq!(expected_data_bytes, actual_data_bytes);

                // We successfully read from a file
                total_bytes_read += length;
                count += 1;
            }
        }
        debug!("Read {} bytes from {} handles", total_bytes_read, count);
        Ok(())
    }

    // Fills the disk with blobs that are |BLOCK_SIZE| bytes in size (when uncompressed)
    // until the filesystem reports an error.
    async fn fill_disk_with_small_blobs(&mut self) -> Result<(), Status> {
        loop {
            // Keep making |BLOCK_SIZE| uncompressible blobs
            let data = self
                .factory
                .create_with_exact_uncompressed_size(BLOCK_SIZE, Compressibility::Uncompressible);

            let blob = Blob::create(data, &self.root_dir).await?;

            // Another blob was created
            self.blobs.push(blob);
        }
    }

    // Removes all blobs from the filesystem
    async fn delete_all_blobs(&mut self) -> Result<(), Status> {
        while self.num_blobs() > 0 {
            self.delete_blob(0).await?;
        }
        Ok(())
    }

    fn num_blobs(&self) -> usize {
        self.blobs.len()
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn get_operation_list(&self) -> Vec<BlobfsOperation> {
        // It is always valid to do the following operations
        let mut operations = vec![
            BlobfsOperation::VerifyBlobs,
            BlobfsOperation::CreateReasonableBlobs,
            BlobfsOperation::FillDiskWithSmallBlobs,
        ];

        if self.num_blobs() > 0 {
            operations.push(BlobfsOperation::DeleteAllBlobs);
            operations.push(BlobfsOperation::DeleteSomeBlobs);
            operations.push(BlobfsOperation::NewHandles);
        }

        let handles_exist = self.blobs.iter().any(|blob| blob.num_handles() > 0);
        if handles_exist {
            operations.push(BlobfsOperation::CloseAllHandles);
            operations.push(BlobfsOperation::ReadFromAllHandles);
        }

        operations
    }

    async fn do_operation(&mut self, operation: &BlobfsOperation) -> Result<(), Status> {
        match operation {
            BlobfsOperation::VerifyBlobs => self.verify_blobs().await,
            BlobfsOperation::CreateReasonableBlobs => self.create_reasonable_blobs().await,
            BlobfsOperation::FillDiskWithSmallBlobs => self.fill_disk_with_small_blobs().await,
            BlobfsOperation::DeleteAllBlobs => self.delete_all_blobs().await,
            BlobfsOperation::NewHandles => self.new_handles().await,
            BlobfsOperation::DeleteSomeBlobs => self.delete_some_blobs().await,
            BlobfsOperation::CloseAllHandles => self.close_all_handles().await,
            BlobfsOperation::ReadFromAllHandles => self.read_from_all_handles().await,
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
                // Reconnect to the blobfs mount point
                self.root_dir = wait_for_blobfs_dir().await;

                // Drain all open handles. These are now invalid.
                for blob in &mut self.blobs {
                    blob.handles().drain(..);
                }
            }

            debug!("{} <<<<---- {:?} [{:?}]", index, operation, result);
        }
    }
}
