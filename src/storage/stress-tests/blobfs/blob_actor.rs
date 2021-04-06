// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{
        SeekOrigin, OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE,
    },
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::Status,
    log::debug,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    storage_stress_test_utils::{
        data::{Compressibility, FileData},
        io::Directory,
    },
    stress_test::actor::{Actor, ActorError},
};

const BLOCK_SIZE: u64 = fuchsia_merkle::BLOCK_SIZE as u64;

/// The list of operations that this operator supports
#[derive(Debug)]
enum BlobfsOperation {
    // Fill disk with files that are reasonable in size ([8, 4096] KiB)
    FillDiskWithReasonableBlobs,

    // Fill the disk with files that are 8KiB in size
    FillDiskWithSmallBlobs,

    // Read a random amount of data at a random offset from a random blob
    ReadFromBlob,

    // Create a large blob that does not fit on disk and expect it to fail
    CreateBlobThatDoesntFitOnDisk,
}

// Performs operations on blobs expected to exist on disk
pub struct BlobActor {
    // Random number generator used by the operator
    pub rng: SmallRng,

    // Size of the entire disk. This is used for creating a blob larger than the disk.
    pub disk_size: u64,

    // Blobfs root directory
    pub root_dir: Directory,
}

impl BlobActor {
    async fn create_blob(&self, data: FileData) -> Result<(), Status> {
        // Create the root hash for the blob
        let data_bytes = data.generate_bytes();
        let tree = MerkleTree::from_reader(&data_bytes[..]).unwrap();
        let merkle_root_hash = tree.root().to_string();

        // Write the file to disk
        let file = self
            .root_dir
            .open_file(
                &merkle_root_hash,
                OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_WRITABLE,
            )
            .await?;
        file.truncate(data.size_bytes).await?;
        file.write(&data_bytes).await?;
        file.close().await
    }

    // Creates reasonable-sized blob to fill a percentage of the free space
    // available on disk.
    async fn fill_disk_with_reasonable_blobs(&mut self) -> Result<(), Status> {
        debug!("Creating reasonable-sized blob");

        loop {
            // Create a blob whose uncompressed size is reasonable, or exactly the requested size
            // if the requested size is too small.
            let data =
                FileData::new_with_reasonable_size(&mut self.rng, Compressibility::Compressible);

            self.create_blob(data).await?;
        }
    }

    // Read a random amount of data at a random offset from a random blob
    async fn read_from_blob(&mut self) -> Result<(), Status> {
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
        let offset = self.rng.gen_range(0, data_size_bytes - 1);

        // Determine the length of this read
        let end_pos = self.rng.gen_range(offset, data_size_bytes);

        assert!(end_pos >= offset);
        let length = end_pos - offset;

        // Read the data from the handle and verify it
        file.seek(SeekOrigin::Start, offset).await?;
        let actual_data_bytes = file.read_num_bytes(length).await?;
        assert_eq!(actual_data_bytes.len(), length as usize);

        debug!("Read {} bytes from {}", length, blob);
        Ok(())
    }

    // Fills the disk with blobs that are |BLOCK_SIZE| bytes in size (when uncompressed)
    // until the filesystem reports an error.
    async fn fill_disk_with_small_blobs(&mut self) -> Result<(), Status> {
        debug!("Filling disk with small blobs");
        loop {
            // Keep making |BLOCK_SIZE| uncompressible blobs
            let data = FileData::new_with_exact_uncompressed_size(
                &mut self.rng,
                BLOCK_SIZE,
                Compressibility::Uncompressible,
            );

            self.create_blob(data).await?;
        }
    }

    // Create a large blob that doesn't fit on disk. This is expected to fail.
    async fn create_blob_that_doesnt_fit_on_disk(&mut self) -> Result<(), Status> {
        debug!("Creating blob that doesn't fit on disk");
        let data = FileData::new_with_exact_uncompressed_size(
            &mut self.rng,
            self.disk_size * 2,
            Compressibility::Uncompressible,
        );

        match self.create_blob(data).await {
            Ok(()) => panic!("Creating a blob larger than disk should have failed"),
            Err(e) => Err(e),
        }
    }

    // Returns a list of operations that are valid to perform,
    // given the current state of the filesystem.
    fn valid_operations(&self) -> Vec<BlobfsOperation> {
        vec![
            BlobfsOperation::ReadFromBlob,
            BlobfsOperation::FillDiskWithReasonableBlobs,
            BlobfsOperation::FillDiskWithSmallBlobs,
            BlobfsOperation::CreateBlobThatDoesntFitOnDisk,
        ]
    }
}

#[async_trait]
impl Actor for BlobActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        let operations = self.valid_operations();
        let operation = operations.choose(&mut self.rng).unwrap();

        let result = match operation {
            BlobfsOperation::FillDiskWithReasonableBlobs => {
                self.fill_disk_with_reasonable_blobs().await
            }
            BlobfsOperation::FillDiskWithSmallBlobs => self.fill_disk_with_small_blobs().await,
            BlobfsOperation::ReadFromBlob => self.read_from_blob().await,
            BlobfsOperation::CreateBlobThatDoesntFitOnDisk => {
                self.create_blob_that_doesnt_fit_on_disk().await
            }
        };

        match result {
            Ok(()) => Ok(()),
            Err(Status::NOT_FOUND) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            Err(Status::CONNECTION_ABORTED)
            | Err(Status::PEER_CLOSED)
            | Err(Status::IO)
            | Err(Status::IO_REFUSED) => Err(ActorError::ResetEnvironment),
            Err(s) => panic!("Error occurred during {:?}: {}", operation, s),
        }
    }
}
