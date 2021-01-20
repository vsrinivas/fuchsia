// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{blob::Blob, instance::BlobfsInstance},
    async_trait::async_trait,
    fidl_fuchsia_io::{SeekOrigin, OPEN_RIGHT_READABLE},
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    stress_test_utils::{
        actor::{Actor, ActorError},
        data::{Compressibility, FileData},
    },
};

const BLOCK_SIZE: u64 = fuchsia_merkle::BLOCK_SIZE as u64;

/// The list of operations that this operator supports
#[derive(Debug)]
enum BlobfsOperation {
    // Create [1, 200] files that are reasonable in size ([8, 4096] KiB)
    CreateReasonableBlobs,

    // Fill the disk with files that are 8KiB in size
    FillDiskWithSmallBlobs,

    // Open handles to a random number of blobs on disk
    NewHandles,

    // Close all open handles to all blobs
    CloseAllHandles,

    // From all open handles, read a random amount of data from a random offset
    ReadFromAllHandles,

    // Verify the contents of all blobs on disk
    VerifyBlobs,
}

// Performs operations on blobs expected to exist on disk
pub struct BlobActor {
    // In-memory representations of all blobs as they exist on disk
    blobs: Vec<Blob>,

    // Random number generator used by the operator
    rng: SmallRng,
}

impl BlobActor {
    pub fn new(rng: SmallRng) -> Self {
        Self { blobs: vec![], rng }
    }

    fn hashes(&self) -> Vec<String> {
        self.blobs.iter().map(|b| b.merkle_root_hash().to_string()).collect()
    }

    // Reads in all blobs stored on the filesystem and compares them to our in-memory
    // model to ensure that everything is as expected.
    async fn verify_blobs(&mut self, instance: &BlobfsInstance) -> Result<(), Status> {
        debug!("Verifying {} blobs...", self.blobs.len());
        let on_disk_hashes = instance.root_dir.entries().await?;

        // Cleanup: Remove all blobs that no longer exist on disk
        let mut blobs = vec![];
        for blob in self.blobs.drain(..) {
            if on_disk_hashes.iter().any(|h| h == blob.merkle_root_hash()) {
                blobs.push(blob);
            }
        }
        self.blobs = blobs;

        let in_memory_hashes = self.hashes();

        for hash in on_disk_hashes {
            if !in_memory_hashes.contains(&hash) {
                panic!("Found blob on disk that does not exist in memory: {}", hash);
            }
        }

        for blob in &self.blobs {
            blob.verify_from_disk(&instance.root_dir).await?;
        }

        Ok(())
    }

    // Creates reasonable-sized blobs to fill a percentage of the free space
    // available on disk.
    async fn create_reasonable_blobs(&mut self, instance: &BlobfsInstance) -> Result<(), Status> {
        let num_blobs_to_create: u64 = self.rng.gen_range(1, 200);
        debug!("Creating {} reasonable-size blobs...", num_blobs_to_create);

        // Start filling the space with blobs
        for _ in 1..=num_blobs_to_create {
            // Create a blob whose uncompressed size is reasonable, or exactly the requested size
            // if the requested size is too small.
            let data =
                FileData::new_with_reasonable_size(&mut self.rng, Compressibility::Compressible);
            let blob = Blob::create(data, &instance.root_dir).await?;

            // Another blob was created
            self.blobs.push(blob);
        }

        Ok(())
    }

    // Creates open handles for a random number of blobs
    async fn new_handles(&mut self, instance: &BlobfsInstance) -> Result<(), Status> {
        // Decide how many blobs to create new handles for
        let num_blobs_with_new_handles = self.rng.gen_range(0, self.num_blobs());
        debug!("Creating handles for {} blobs", num_blobs_with_new_handles);

        // Randomly select blobs from the list and create handles to them
        for _ in 0..num_blobs_with_new_handles {
            // Choose a random blob and open a handle to it
            let blob = self.blobs.choose_mut(&mut self.rng).unwrap();
            let handle =
                instance.root_dir.open_file(blob.merkle_root_hash(), OPEN_RIGHT_READABLE).await?;
            blob.handles().push(handle);
        }

        Ok(())
    }

    // Closes all open handles to all blobs.
    // Note that handles do not call `close()` when they are dropped.
    // This is intentional because it offers a way to inelegantly close a file.
    async fn close_all_handles(&mut self) -> Result<(), Status> {
        debug!("Closing all open handles");
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
        debug!("Reading from all open handles");
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
    async fn fill_disk_with_small_blobs(
        &mut self,
        instance: &BlobfsInstance,
    ) -> Result<(), Status> {
        loop {
            // Keep making |BLOCK_SIZE| uncompressible blobs
            let data = FileData::new_with_exact_uncompressed_size(
                &mut self.rng,
                BLOCK_SIZE,
                Compressibility::Uncompressible,
            );

            let blob = Blob::create(data, &instance.root_dir).await?;

            // Another blob was created
            self.blobs.push(blob);
        }
    }

    fn num_blobs(&self) -> usize {
        self.blobs.len()
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn valid_operations(&self) -> Vec<BlobfsOperation> {
        // It is always valid to do the following operations
        let mut operations = vec![
            BlobfsOperation::VerifyBlobs,
            BlobfsOperation::CreateReasonableBlobs,
            BlobfsOperation::FillDiskWithSmallBlobs,
        ];

        if self.num_blobs() > 0 {
            operations.push(BlobfsOperation::NewHandles);
        }

        let handles_exist = self.blobs.iter().any(|blob| blob.num_handles() > 0);
        if handles_exist {
            operations.push(BlobfsOperation::CloseAllHandles);
            operations.push(BlobfsOperation::ReadFromAllHandles);
        }

        operations
    }
}

#[async_trait]
impl Actor<BlobfsInstance> for BlobActor {
    async fn perform(&mut self, instance: &mut BlobfsInstance) -> Result<(), ActorError> {
        let operations = self.valid_operations();
        let operation = operations.choose(&mut self.rng).unwrap();

        let result = match operation {
            BlobfsOperation::VerifyBlobs => self.verify_blobs(instance).await,
            BlobfsOperation::CreateReasonableBlobs => self.create_reasonable_blobs(instance).await,
            BlobfsOperation::FillDiskWithSmallBlobs => {
                self.fill_disk_with_small_blobs(instance).await
            }
            BlobfsOperation::NewHandles => self.new_handles(instance).await,
            BlobfsOperation::CloseAllHandles => self.close_all_handles().await,
            BlobfsOperation::ReadFromAllHandles => self.read_from_all_handles().await,
        };

        match result {
            Ok(()) => Ok(()),
            Err(Status::NOT_FOUND) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            Err(Status::CONNECTION_ABORTED) | Err(Status::PEER_CLOSED) => {
                // Drain out all the open handles.
                // Do not bother closing them properly. They are about to become invalid.
                for blob in &mut self.blobs {
                    blob.handles().drain(..);
                }
                Err(ActorError::GetNewInstance)
            }
            Err(s) => panic!("Error occurred during {:?}: {}", operation, s),
        }
    }
}
