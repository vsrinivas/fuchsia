// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rand::{rngs::SmallRng, seq::SliceRandom, Rng},
    std::cmp::min,
};

/// Controls the compressibility of data generated for a file.
/// This parameter is only meaningful for filesystems that support transparent compression.
pub enum Compressibility {
    Compressible,
    Uncompressible,
}

// Controls the uncompressed size of the file
pub enum UncompressedSize {
    Exact(u64),
    InRange(u64, u64),
}

/// Run lengths and bytes are sampled from a uniform distribution.
/// Data created by this function achieves roughly 50% size reduction after compression.
fn generate_compressible_data_bytes(rng: &mut SmallRng, size_bytes: u64) -> Vec<u8> {
    let mut bytes: Vec<u8> = Vec::with_capacity(size_bytes as usize);

    // The file is filled with compressible runs of one of the following bytes.
    let byte_choices: [u8; 6] = [0xde, 0xad, 0xbe, 0xef, 0x42, 0x0];
    let mut ptr = 0;
    while ptr < size_bytes {
        // A run is 10..1024 bytes long
        let mut run_length = rng.gen_range(10..1024);

        // In case the run goes past the file size
        run_length = min(run_length, size_bytes - ptr);

        // Decide whether this run should be compressible or not.
        // This results in files that compress reasonably well (target 50% size reduction).
        if rng.gen_bool(0.5) {
            // Choose a byte for this run.
            let choice = byte_choices.choose(rng).unwrap();

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
fn generate_uncompressible_data_bytes(rng: &mut SmallRng, size_bytes: u64) -> Vec<u8> {
    let mut bytes: Vec<u8> = Vec::with_capacity(size_bytes as usize);
    for _ in 0..size_bytes {
        bytes.push(rng.gen());
    }
    bytes
}

/// A compact in-memory representation of data that can be stored in a file.
pub struct FileFactory {
    pub rng: SmallRng,
    pub uncompressed_size: UncompressedSize,
    pub compressibility: Compressibility,
    pub file_name_count: u64,
}

impl FileFactory {
    #[must_use]
    pub fn new(
        rng: SmallRng,
        uncompressed_size: UncompressedSize,
        compressibility: Compressibility,
    ) -> Self {
        Self { rng, uncompressed_size, compressibility, file_name_count: 0 }
    }

    /// Generate a unique filename that can be used for a file created by this factory.
    pub fn generate_filename(&mut self) -> String {
        self.file_name_count += 1;
        format!("file_{}", self.file_name_count)
    }

    /// Generate all the bytes for this file in memory.
    /// For a given FileData, this function is deterministic.
    pub fn generate_bytes(&mut self) -> Vec<u8> {
        let size_bytes = match self.uncompressed_size {
            UncompressedSize::Exact(size_bytes) => size_bytes,
            UncompressedSize::InRange(min, max) => self.rng.gen_range(min..max),
        };

        match self.compressibility {
            Compressibility::Compressible => {
                generate_compressible_data_bytes(&mut self.rng, size_bytes)
            }
            Compressibility::Uncompressible => {
                generate_uncompressible_data_bytes(&mut self.rng, size_bytes)
            }
        }
    }
}
