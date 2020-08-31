// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blob::{Blob, BlobDataFactory, Compressibility, CreationError},
    crate::io::Directory,
    crate::utils::BLOCK_SIZE,
    log::{debug, error},
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
};

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

impl BlobfsOperation {
    fn random(rng: &mut SmallRng) -> BlobfsOperation {
        match rng.gen_range(0, 8) {
            0 => BlobfsOperation::CreateReasonableBlobs,
            1 => BlobfsOperation::DeleteSomeBlobs,
            2 => BlobfsOperation::FillDiskWithSmallBlobs,
            3 => BlobfsOperation::DeleteAllBlobs,
            4 => BlobfsOperation::NewHandles,
            5 => BlobfsOperation::CloseAllHandles,
            6 => BlobfsOperation::ReadFromAllHandles,
            _ => BlobfsOperation::VerifyBlobs,
        }
    }
}

// In-memory state of blobfs. Stores information about blobs expected to exist on disk.
pub struct BlobfsState {
    // The path to the blobfs root
    root_dir: Directory,

    // In-memory representations of all blobs as they exist on disk
    blobs: Vec<Blob>,

    // Random number generator used for selecting operations and blobs
    rng: SmallRng,

    // Factory for creating blob data that meets certain specifications
    factory: BlobDataFactory,
}

impl BlobfsState {
    pub fn new(root_dir: Directory, mut initial_rng: SmallRng) -> BlobfsState {
        // Setup the RNGs
        let factory_rng = SmallRng::from_seed(initial_rng.gen());
        let state_rng = SmallRng::from_seed(initial_rng.gen());

        let factory = BlobDataFactory::new(factory_rng);

        BlobfsState { root_dir, blobs: vec![], rng: state_rng, factory }
    }

    // Deletes blob at [index] from the filesystem and from the list of known blobs.
    async fn delete_blob(&mut self, index: usize) -> Blob {
        let blob = self.blobs.remove(index);
        self.root_dir.remove(blob.merkle_root_hash()).await;
        blob
    }

    fn hashes(&self) -> Vec<String> {
        self.blobs.iter().map(|b| b.merkle_root_hash().to_string()).collect()
    }

    // Reads in all blobs stored on the filesystem and compares them to our in-memory
    // model to ensure that everything is as expected.
    async fn verify_blobs(&self) {
        let on_disk_hashes = self.root_dir.entries().await.sort_unstable();
        let in_memory_hashes = self.hashes().sort_unstable();

        assert_eq!(on_disk_hashes, in_memory_hashes);

        for blob in &self.blobs {
            blob.verify_from_disk(&self.root_dir).await;
        }
    }

    // Creates reasonable-sized blobs to fill a percentage of the free space
    // available on disk.
    async fn create_reasonable_blobs(&mut self) {
        let num_blobs_to_create: u64 = self.rng.gen_range(1, 200);
        debug!("Creating {} blobs...", num_blobs_to_create);

        // Start filling the space with blobs
        for _ in 0..num_blobs_to_create {
            // Create a blob whose uncompressed size is reasonable, or exactly the requested size
            // if the requested size is too small.
            let data = self.factory.create_with_reasonable_size(Compressibility::Compressible);

            let result = Blob::create(data, &self.root_dir).await;

            match result {
                Ok(blob) => {
                    // Another blob was created
                    self.blobs.push(blob);
                }
                Err(CreationError::OutOfSpace) => {
                    error!("Ran out of space creating blob");
                    break;
                }
            }
        }
    }

    // Deletes a random number of blobs from the disk
    async fn delete_some_blobs(&mut self) {
        // Do nothing if there are no blobs.
        if self.num_blobs() == 0 {
            return;
        }

        // Decide how many blobs to delete
        let num_blobs_to_delete = self.rng.gen_range(0, self.num_blobs());
        debug!("Deleting {} blobs", num_blobs_to_delete);

        // Randomly select blobs from the list and remove them
        for _ in 0..num_blobs_to_delete {
            let index = self.rng.gen_range(0, self.num_blobs());
            self.delete_blob(index).await;
        }
    }

    // Creates open handles for a random number of blobs
    async fn new_handles(&mut self) {
        if self.num_blobs() == 0 {
            return;
        }

        // Decide how many blobs to create new handles for
        let num_blobs_with_new_handles = self.rng.gen_range(0, self.num_blobs());
        debug!("Creating handles for {} blobs", num_blobs_with_new_handles);

        // Randomly select blobs from the list and create handles to them
        for _ in 0..num_blobs_with_new_handles {
            // Choose a random blob and open a handle to it
            let blob = self.blobs.choose_mut(&mut self.rng).unwrap();
            let handle = self.root_dir.open(blob.merkle_root_hash()).await;
            blob.handles().push(handle);
            debug!("Opened blob {} [handles:{}]", blob.merkle_root_hash(), blob.num_handles());
        }
    }

    // Closes all open handles to all blobs.
    // Note that handles do not call `close()` when they are dropped.
    // This is intentional because it offers a way to inelegantly close a file.
    async fn close_all_handles(&mut self) {
        let mut count = 0;
        for blob in &mut self.blobs {
            for handle in blob.handles().drain(..) {
                handle.close().await;
                count += 1;
            }
        }
        debug!("Closed {} handles to blobs", count);
    }

    // Reads random portions of a blob from all open handles
    async fn read_from_all_handles(&mut self) {
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
                handle.seek_from_start(offset).await;
                let actual_data_bytes = handle.read(length).await;
                let expected_data_bytes = &data_bytes[offset as usize..end_pos as usize];
                assert_eq!(expected_data_bytes, actual_data_bytes);

                // We successfully read from a file
                total_bytes_read += length;
                count += 1;
            }
        }
        debug!("Read {} bytes from {} handles", total_bytes_read, count);
    }

    // Fills the disk with blobs that are |BLOCK_SIZE| bytes in size (when uncompressed)
    // until the filesystem reports an error.
    async fn fill_disk_with_small_blobs(&mut self) {
        let mut blob_count = 0;

        loop {
            // Keep making |BLOCK_SIZE| uncompressible blobs
            let data = self
                .factory
                .create_with_exact_uncompressed_size(BLOCK_SIZE, Compressibility::Uncompressible);

            let result = Blob::create(data, &self.root_dir).await;

            match result {
                Ok(blob) => {
                    // Another blob was created
                    self.blobs.push(blob);
                    blob_count += 1;
                }
                Err(CreationError::OutOfSpace) => {
                    error!("Ran out of space creating blob");
                    break;
                }
            }
        }

        debug!("Created {} blobs", blob_count);
    }

    // Removes all blobs from the filesystem
    async fn delete_all_blobs(&mut self) {
        while self.num_blobs() > 0 {
            self.delete_blob(0).await;
        }
    }

    pub async fn do_random_operation(&mut self) {
        let operation = BlobfsOperation::random(&mut self.rng);
        debug!("-------> [OPERATION] {:?}", operation);
        debug!("Number of blobs = {}", self.num_blobs());
        match operation {
            BlobfsOperation::CreateReasonableBlobs => {
                self.create_reasonable_blobs().await;
            }
            BlobfsOperation::DeleteSomeBlobs => {
                self.delete_some_blobs().await;
            }
            BlobfsOperation::FillDiskWithSmallBlobs => {
                self.fill_disk_with_small_blobs().await;
            }
            BlobfsOperation::DeleteAllBlobs => {
                self.delete_all_blobs().await;
            }
            BlobfsOperation::NewHandles => {
                self.new_handles().await;
            }
            BlobfsOperation::CloseAllHandles => {
                self.close_all_handles().await;
            }
            BlobfsOperation::ReadFromAllHandles => {
                self.read_from_all_handles().await;
            }
            BlobfsOperation::VerifyBlobs => {
                self.verify_blobs().await;
            }
        }
        debug!("<------- [OPERATION] {:?}", operation);
    }

    fn num_blobs(&self) -> usize {
        self.blobs.len()
    }
}
