// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::blob::{Blob, BlobFactory, Compressibility, CreationError},
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
    NewHandle,
    CloseAllHandles,
    VerifyBlobs,
}

impl BlobfsOperation {
    fn random(rng: &mut SmallRng) -> BlobfsOperation {
        match rng.gen_range(0, 7) {
            0 => BlobfsOperation::CreateReasonableBlobs,
            1 => BlobfsOperation::DeleteSomeBlobs,
            2 => BlobfsOperation::FillDiskWithSmallBlobs,
            3 => BlobfsOperation::DeleteAllBlobs,
            4 => BlobfsOperation::NewHandle,
            5 => BlobfsOperation::CloseAllHandles,
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

    // Factory for writing blobs to disk that meet certain specifications
    factory: BlobFactory,
}

impl BlobfsState {
    pub fn new(root_dir: Directory, mut initial_rng: SmallRng) -> BlobfsState {
        // Setup the RNGs
        let factory_rng = SmallRng::from_seed(initial_rng.gen());
        let state_rng = SmallRng::from_seed(initial_rng.gen());

        let factory = BlobFactory::new(root_dir.clone(), factory_rng);

        BlobfsState { root_dir, blobs: vec![], rng: state_rng, factory }
    }

    // Deletes blob at [index] from the filesystem and from the list of known blobs.
    async fn delete_blob(&mut self, index: usize) -> Blob {
        let blob = self.blobs.remove(index);
        self.root_dir.remove(blob.merkle_root_hash()).await;
        blob
    }

    // Reads in all blobs stored on the filesystem and compares them to our in-memory
    // model to ensure that everything is as expected.
    async fn verify_blobs(&self) {
        let entries = self.root_dir.entries().await;

        // Ensure that the number of blobs on disk is correct
        assert_eq!(self.blobs.len(), entries.len());

        // All blobs on disk must correspond to a blob in memory
        for hash in entries {
            let blob = Blob::from_disk(&self.root_dir, &hash).await;
            if !self.blobs.contains(&blob) {
                panic!(
                    "Blob does not exist in memory -> {}, {}, {}",
                    blob.merkle_root_hash(),
                    blob.size_on_disk(),
                    blob.uncompressed_size()
                );
            }
        }

        // All blobs in memory must match a blob on disk
        for blob in &self.blobs {
            let on_disk_blob = Blob::from_disk(&self.root_dir, blob.merkle_root_hash()).await;
            if blob != &on_disk_blob {
                panic!(
                    "Blob is not same as on disk -> {}, {}, {}",
                    blob.merkle_root_hash(),
                    blob.size_on_disk(),
                    blob.uncompressed_size()
                );
            }
        }
    }

    // Creates reasonable-sized blobs to fill a percentage of the free space
    // available on disk.
    async fn create_reasonable_blobs(&mut self) {
        let num_blobs_to_create = self.rng.gen_range(1, 200);
        debug!("Creating {} blobs...", num_blobs_to_create);

        // Start filling the space with blobs
        for _ in 0..num_blobs_to_create {
            // Create a blob whose uncompressed size is reasonable, or exactly the requested size
            // if the requested size is too small.
            let result =
                self.factory.create_with_reasonable_size(Compressibility::Compressible).await;

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

    // Selects a random blob and creates an open handle for it.
    async fn new_handle(&mut self) {
        if self.num_blobs() == 0 {
            return;
        }

        // Choose a random blob and open a handle to it
        let blob = self.blobs.choose_mut(&mut self.rng).unwrap();
        let handle = self.root_dir.open(blob.merkle_root_hash()).await;
        blob.handles().push(handle);

        debug!("Opened blob {} [handles:{}]", blob.merkle_root_hash(), blob.num_handles());
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

    // Fills the disk with blobs that are |BLOCK_SIZE| bytes in size (when uncompressed)
    // until the filesystem reports an error.
    async fn fill_disk_with_small_blobs(&mut self) {
        let mut blob_count = 0;

        loop {
            // Keep making |BLOCK_SIZE| uncompressible blobs
            let result = self
                .factory
                .create_with_exact_uncompressed_size(BLOCK_SIZE, Compressibility::Uncompressible)
                .await;

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
            BlobfsOperation::NewHandle => {
                self.new_handle().await;
            }
            BlobfsOperation::CloseAllHandles => {
                self.close_all_handles().await;
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
