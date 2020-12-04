// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rand::{rngs::SmallRng, seq::SliceRandom, Rng, SeedableRng},
    std::cmp::min,
};

const EIGHT_KIB: u64 = 8192;
const ONE_MB: u64 = 1048576;
const FOUR_MB: u64 = 4 * ONE_MB;

/// Controls the compressibility of data generated for a file
pub enum Compressibility {
    Compressible,
    Uncompressible,
}

/// Run lengths and bytes are sampled from a uniform distribution.
/// Data created by this function achieves roughly 50% size reduction after compression.
fn generate_compressible_data_bytes(mut rng: SmallRng, size_bytes: u64) -> Vec<u8> {
    let mut bytes: Vec<u8> = Vec::with_capacity(size_bytes as usize);

    // The file is filled with compressible runs of one of the following bytes.
    let byte_choices: [u8; 6] = [0xde, 0xad, 0xbe, 0xef, 0x42, 0x0];
    let mut ptr = 0;
    while ptr < size_bytes {
        // A run is 10..1024 bytes long
        let mut run_length = rng.gen_range(10, 1024);

        // In case the run goes past the file size
        run_length = min(run_length, size_bytes - ptr);

        // Decide whether this run should be compressible or not.
        // This results in files that compress reasonably well (target 50% size reduction).
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

    // The file must be of the expected size
    assert!(bytes.len() == size_bytes as usize);

    bytes
}

/// Bytes are sampled from a uniform distribution.
/// Data created by this function compresses badly.
fn generate_uncompressible_data_bytes(mut rng: SmallRng, size_bytes: u64) -> Vec<u8> {
    let mut bytes: Vec<u8> = Vec::with_capacity(size_bytes as usize);
    for _ in 0..size_bytes {
        bytes.push(rng.gen());
    }
    bytes
}

/// A compact in-memory representation of data that can be stored in a file.
pub struct FileData {
    pub seed: u128,
    pub size_bytes: u64,
    pub compressibility: Compressibility,
}

impl FileData {
    fn new(seed: u128, size_bytes: u64, compressibility: Compressibility) -> Self {
        Self { seed, size_bytes, compressibility }
    }

    /// Generate all the bytes for this file in memory.
    /// For a given FileData, this function is deterministic.
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

pub struct FileDataFactory {
    rng: SmallRng,
}

impl FileDataFactory {
    pub fn new(rng: SmallRng) -> Self {
        Self { rng }
    }

    /// Create a file whose uncompressed size is reasonable (between 8KiB and 4MiB)
    #[must_use]
    pub fn create_with_reasonable_size(&mut self, compressibility: Compressibility) -> FileData {
        self.create_with_uncompressed_size_in_range(EIGHT_KIB, FOUR_MB, compressibility)
    }

    /// Create a file whose uncompressed size is in the range requested.
    /// The exact size of the file is chosen from a uniform distribution.
    #[must_use]
    pub fn create_with_uncompressed_size_in_range(
        &mut self,
        min_uncompressed_size_bytes: u64,
        max_uncompressed_size_bytes: u64,
        compressibility: Compressibility,
    ) -> FileData {
        let uncompressed_size =
            self.rng.gen_range(min_uncompressed_size_bytes, max_uncompressed_size_bytes);
        self.create_with_exact_uncompressed_size(uncompressed_size, compressibility)
    }

    /// Create a file whose uncompressed size is exactly as requested
    #[must_use]
    pub fn create_with_exact_uncompressed_size(
        &mut self,
        uncompressed_size_bytes: u64,
        compressibility: Compressibility,
    ) -> FileData {
        FileData::new(self.rng.gen(), uncompressed_size_bytes, compressibility)
    }
}
