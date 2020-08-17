// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils::{BLOCK_SIZE, FOUR_MB, OUT_OF_SPACE_OS_ERROR_CODE},
    fuchsia_merkle::MerkleTree,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    std::{
        cmp::{min, Eq},
        fs::{metadata, read, File},
        hash::{Hash, Hasher},
        io::Write,
        os::unix::fs::MetadataExt,
        path::PathBuf,
    },
};

// In-memory representation of a single blob.
#[derive(Debug)]
pub struct Blob {
    // The qualified path to this blob
    path: PathBuf,

    // The root merkle hash for this blob
    merkle_root_hash: String,

    // The uncompressed bytes that constitute the data of this blob
    // TODO(xbhatnag): Rather than storing the data, store a seed for a RNG
    // which can be used to reproduce this data.
    data: Vec<u8>,

    // The size of this blob as measured on disk.
    size_on_disk_bytes: u64,

    // Open handles to this file
    handles: Vec<File>,
}

impl Hash for Blob {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.merkle_root_hash.hash(state);
        self.data.hash(state);
        self.size_on_disk_bytes.hash(state);
    }
}

impl Eq for Blob {}

impl PartialEq for Blob {
    fn eq(&self, other: &Self) -> bool {
        self.merkle_root_hash == other.merkle_root_hash
            && self.size_on_disk_bytes == other.size_on_disk_bytes
    }
}
// Controls the compressibility of data generated for a blob
pub enum Compressibility {
    Compressible,
    Uncompressible,
}

// Reasons why creating a blob can fail
#[derive(Debug)]
pub enum CreationError {
    // Hold onto the file that failed creation so that it doesn't close.
    OutOfSpace(File),
}

impl Blob {
    // Attempts to write the blob to disk. This operation is allowed to fail
    // if we run out of storage space. Other failures cause a panic.
    fn create(root_path: &PathBuf, data: Vec<u8>) -> Result<Blob, CreationError> {
        // Create the root hash for the blob
        let tree = MerkleTree::from_reader(&data[..]).unwrap();
        let merkle_root_hash = tree.root().to_string();

        // Write the file to disk
        let path = root_path.join(&merkle_root_hash);
        let mut file = File::create(&path).unwrap();
        file.set_len(data.len() as u64).unwrap();
        let result = file.write(&data);

        // The only error we will forward is running out of storage space.
        // Note that blobfs will return this error code for running out of inodes as well.
        if let Err(e) = result {
            if let Some(OUT_OF_SPACE_OS_ERROR_CODE) = e.raw_os_error() {
                return Err(CreationError::OutOfSpace(file));
            } else {
                // TODO(xbhatnag): Bubble this error up to the top loop.
                // Use panics for unrecoverable error states from SUT.
                // Use asserts to prevent programmer error.
                panic!("Unexpected error during write: {}", e);
            }
        }

        file.flush().unwrap();

        // Get the size as reported by the filesystem
        let metadata = file.metadata().unwrap();
        let size_on_disk_bytes = metadata.blocks() * metadata.blksize();

        let blob = Blob { path, merkle_root_hash, data, size_on_disk_bytes, handles: vec![] };

        blob.check_well_formed();

        Ok(blob)
    }

    // Reads the blob that matches this hash from disk.
    // This operation is not expected to fail.
    pub fn from_disk(root_path: &PathBuf, merkle_root_hash: &str) -> Blob {
        let path = root_path.join(merkle_root_hash);
        let data = read(&path).unwrap();
        let metadata = metadata(&path).unwrap();
        let size_on_disk_bytes = metadata.blocks() * metadata.blksize();

        let blob = Blob {
            path,
            merkle_root_hash: merkle_root_hash.to_string(),
            data,
            size_on_disk_bytes,
            handles: vec![],
        };

        blob.check_well_formed();

        blob
    }

    // Do some basic checks on the blob
    fn check_well_formed(&self) {
        // Verify the root hash of this blob
        let tree = MerkleTree::from_reader(&self.data[..]).unwrap();
        let merkle_root_hash = tree.root().to_string();

        // TODO(xbhatnag): Bubble this error up to the top loop.
        // Use panics for unrecoverable error states from SUT.
        // Use asserts to prevent programmer error.
        assert!(self.merkle_root_hash == merkle_root_hash);
    }

    // Returns the size of the blob data as uncompressed in memory
    pub fn uncompressed_size(&self) -> u64 {
        self.data.len() as u64
    }

    // Returns the size of the blob on disk (including the size of the merkle tree)
    pub fn size_on_disk(&self) -> u64 {
        self.size_on_disk_bytes
    }

    pub fn merkle_root_hash(&self) -> &str {
        &self.merkle_root_hash
    }

    pub fn data(&self) -> &Vec<u8> {
        &self.data
    }

    pub fn path(&self) -> &PathBuf {
        &self.path
    }

    // Creates a new handle to this blob
    pub fn open(&self) -> File {
        File::open(&self.path).unwrap()
    }

    // Returns the list of open handles to this blob.
    // Placing a file in this list ensures that it remains open.
    pub fn handles(&mut self) -> &mut Vec<File> {
        &mut self.handles
    }

    pub fn num_handles(&self) -> u64 {
        self.handles.len() as u64
    }
}

pub struct BlobFactory {
    rng: SmallRng,
    root_path: PathBuf,
}

impl BlobFactory {
    pub fn new(root_path: PathBuf, rng: SmallRng) -> Self {
        BlobFactory { root_path, rng }
    }

    // Run lengths and bytes are sampled from a uniform distribution.
    // Data created by this function achieves roughly 50% size reduction after compression.
    fn create_compressible_data(&mut self, size_bytes: u64) -> Vec<u8> {
        let mut data: Vec<u8> = vec![];

        // The blob is filled with compressible runs of one of the following bytes.
        let byte_choices: [u8; 6] = [0xde, 0xad, 0xbe, 0xef, 0x42, 0x0];
        let mut ptr = 0;
        while ptr < size_bytes {
            // A run is 10..1024 bytes long
            let mut run_length = self.rng.gen_range(10, 1024);

            // In case the run goes past the blob size
            run_length = min(run_length, size_bytes - ptr);

            // Decide whether this run should be compressible or not.
            // This results in blobs that compress reasonably well (target 50% size reduction).
            if self.rng.gen_bool(0.5) {
                // Choose a byte for this run.
                let choice = byte_choices.choose(&mut self.rng).unwrap();

                // Generate a run of compressible data.
                for _ in 0..run_length {
                    data.push(*choice);
                }
            } else {
                // Generate a run of random data.
                for _ in 0..run_length {
                    data.push(self.rng.gen());
                }
            }

            ptr += run_length;
        }

        // The blob must be of the expected size
        assert!(data.len() == size_bytes as usize);

        data
    }

    // Bytes are sampled from a uniform distribution.
    // Data created by this function compresses badly.
    fn create_uncompressible_data(&mut self, size_bytes: u64) -> Vec<u8> {
        let mut data: Vec<u8> = vec![];
        for _ in 0..size_bytes {
            data.push(self.rng.gen());
        }
        data
    }

    // Create a blob whose uncompressed size is reasonable (between BLOCK_SIZE and 4MB)
    #[must_use]
    pub async fn create_with_reasonable_size(
        &mut self,
        compressibility: Compressibility,
    ) -> Result<Blob, CreationError> {
        self.create_with_uncompressed_size_in_range(BLOCK_SIZE, FOUR_MB, compressibility).await
    }

    // Create a blob whose uncompressed size is in the range requested.
    // The exact size of the blob is chosen from a uniform distribution.
    #[must_use]
    pub async fn create_with_uncompressed_size_in_range(
        &mut self,
        min_uncompressed_size_bytes: u64,
        max_uncompressed_size_bytes: u64,
        compressibility: Compressibility,
    ) -> Result<Blob, CreationError> {
        let uncompressed_size =
            self.rng.gen_range(min_uncompressed_size_bytes, max_uncompressed_size_bytes);
        self.create_with_exact_uncompressed_size(uncompressed_size, compressibility).await
    }

    // Create a blob whose uncompressed size is exactly as requested
    #[must_use]
    pub async fn create_with_exact_uncompressed_size(
        &mut self,
        uncompressed_size_bytes: u64,
        compressibility: Compressibility,
    ) -> Result<Blob, CreationError> {
        let data = match compressibility {
            Compressibility::Compressible => self.create_compressible_data(uncompressed_size_bytes),
            Compressibility::Uncompressible => {
                self.create_uncompressible_data(uncompressed_size_bytes)
            }
        };

        Blob::create(&self.root_path, data)
    }
}
