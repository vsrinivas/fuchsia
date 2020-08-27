// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::io::{Directory, File},
    crate::utils::{BLOCK_SIZE, FOUR_MB},
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::Status,
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    std::cmp::min,
};

// Controls the compressibility of data generated for a blob
pub enum Compressibility {
    Compressible,
    Uncompressible,
}

// Run lengths and bytes are sampled from a uniform distribution.
// Data created by this function achieves roughly 50% size reduction after compression.
fn generate_compressible_data_bytes(mut rng: SmallRng, size_bytes: u64) -> Vec<u8> {
    let mut bytes: Vec<u8> = vec![];

    // The blob is filled with compressible runs of one of the following bytes.
    let byte_choices: [u8; 6] = [0xde, 0xad, 0xbe, 0xef, 0x42, 0x0];
    let mut ptr = 0;
    while ptr < size_bytes {
        // A run is 10..1024 bytes long
        let mut run_length = rng.gen_range(10, 1024);

        // In case the run goes past the blob size
        run_length = min(run_length, size_bytes - ptr);

        // Decide whether this run should be compressible or not.
        // This results in blobs that compress reasonably well (target 50% size reduction).
        if rng.gen_bool(0.5) {
            // Choose a byte for this run.
            let choice = byte_choices.choose(&mut rng).unwrap();

            // Generate a run of compressible data.
            for _ in 0..run_length {
                bytes.push(*choice);
            }
        } else {
            // Generate a run of random data.
            for _ in 0..run_length {
                bytes.push(rng.gen());
            }
        }

        ptr += run_length;
    }

    // The blob must be of the expected size
    assert!(bytes.len() == size_bytes as usize);

    bytes
}

// Bytes are sampled from a uniform distribution.
// Data created by this function compresses badly.
fn generate_uncompressible_data_bytes(mut rng: SmallRng, size_bytes: u64) -> Vec<u8> {
    let mut bytes: Vec<u8> = vec![];
    for _ in 0..size_bytes {
        bytes.push(rng.gen());
    }
    bytes
}

pub struct BlobData {
    pub seed: u128,
    pub size_bytes: u64,
    pub compressibility: Compressibility,
}

impl BlobData {
    fn new(seed: u128, size_bytes: u64, compressibility: Compressibility) -> Self {
        Self { seed, size_bytes, compressibility }
    }

    pub fn generate_bytes(&self) -> Vec<u8> {
        let rng = SmallRng::from_seed(self.seed.to_le_bytes());
        match self.compressibility {
            Compressibility::Compressible => generate_compressible_data_bytes(rng, self.size_bytes),
            Compressibility::Uncompressible => {
                generate_uncompressible_data_bytes(rng, self.size_bytes)
            }
        }
    }
}

// Reasons why creating a blob can fail
#[derive(Debug)]
pub enum CreationError {
    OutOfSpace,
}

pub struct Blob {
    merkle_root_hash: String,
    data: BlobData,
    handles: Vec<File>,
}

impl Blob {
    // Attempts to write the blob to disk. This operation is allowed to fail
    // if we run out of storage space. Other failures cause a panic.
    pub async fn create(data: BlobData, root_dir: &Directory) -> Result<Self, CreationError> {
        // Create the root hash for the blob
        let data_bytes = data.generate_bytes();
        let tree = MerkleTree::from_reader(&data_bytes[..]).unwrap();
        let merkle_root_hash = tree.root().to_string();

        // Write the file to disk
        let file = root_dir.create(&merkle_root_hash).await;
        file.truncate(data.size_bytes).await;

        let result = file.write(&data_bytes).await;

        match result {
            Err(Status::NO_SPACE) => {
                return Err(CreationError::OutOfSpace);
            }
            Err(x) => {
                panic!("Unexpected error during write: {}", x);
            }
            _ => {}
        }

        file.flush().await;
        file.close().await;

        Ok(Self { merkle_root_hash, data, handles: vec![] })
    }

    // Reads the blob in from disk and verifies its contents
    pub async fn verify_from_disk(&self, root_dir: &Directory) {
        let file = root_dir.open(&self.merkle_root_hash).await;
        let on_disk_data_bytes = file.read_until_eof().await;
        file.close().await;

        let in_memory_data_bytes = self.data.generate_bytes();

        assert!(on_disk_data_bytes == in_memory_data_bytes);
    }

    pub fn merkle_root_hash(&self) -> &str {
        &self.merkle_root_hash
    }

    pub fn handles(&mut self) -> &mut Vec<File> {
        &mut self.handles
    }

    pub fn num_handles(&self) -> u64 {
        self.handles.len() as u64
    }
}

pub struct BlobDataFactory {
    rng: SmallRng,
}

impl BlobDataFactory {
    pub fn new(rng: SmallRng) -> Self {
        Self { rng }
    }

    // Create a blob whose uncompressed size is reasonable (between BLOCK_SIZE and 4MB)
    #[must_use]
    pub fn create_with_reasonable_size(&mut self, compressibility: Compressibility) -> BlobData {
        self.create_with_uncompressed_size_in_range(BLOCK_SIZE, FOUR_MB, compressibility)
    }

    // Create a blob whose uncompressed size is in the range requested.
    // The exact size of the blob is chosen from a uniform distribution.
    #[must_use]
    pub fn create_with_uncompressed_size_in_range(
        &mut self,
        min_uncompressed_size_bytes: u64,
        max_uncompressed_size_bytes: u64,
        compressibility: Compressibility,
    ) -> BlobData {
        let uncompressed_size =
            self.rng.gen_range(min_uncompressed_size_bytes, max_uncompressed_size_bytes);
        self.create_with_exact_uncompressed_size(uncompressed_size, compressibility)
    }

    // Create a blob whose uncompressed size is exactly as requested
    #[must_use]
    pub fn create_with_exact_uncompressed_size(
        &mut self,
        uncompressed_size_bytes: u64,
        compressibility: Compressibility,
    ) -> BlobData {
        BlobData::new(self.rng.gen(), uncompressed_size_bytes, compressibility)
    }
}
