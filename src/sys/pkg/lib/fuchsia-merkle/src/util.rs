// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use mundane::hash::{Digest, Hasher, Sha256};
use std::mem::{size_of, size_of_val};

use crate::{Hash, BLOCK_SIZE, HASH_SIZE};

pub(crate) const HASHES_PER_BLOCK: usize = BLOCK_SIZE / HASH_SIZE;

type BlockIdentity = [u8; size_of::<u64>() + size_of::<u32>()];

/// Generate the bytes representing a block's identity.
fn make_identity(length: usize, level: usize, offset: usize) -> BlockIdentity {
    let offset_or_level = (offset as u64 | level as u64).to_le_bytes();
    let length = (length as u32).to_le_bytes();
    let mut ret: BlockIdentity = [0; size_of::<BlockIdentity>()];
    let (ret_offset_or_level, ret_length) = ret.split_at_mut(size_of_val(&offset_or_level));
    ret_offset_or_level.copy_from_slice(&offset_or_level);
    ret_length.copy_from_slice(&length);
    ret
}

/// Compute the merkle hash of a block of data.
///
/// A merkle hash is the SHA-256 hash of a block of data with a small header built from the length
/// of the data, the level of the tree (0 for data blocks), and the offset into the level. The
/// block will be zero filled if its len is less than [`BLOCK_SIZE`], except for when the first
/// data block is completely empty.
///
/// # Panics
///
/// Panics if `block.len()` exceeds [`BLOCK_SIZE`] or if `offset` is not aligned to [`BLOCK_SIZE`]
pub(crate) fn hash_block(block: &[u8], offset: usize) -> Hash {
    assert!(block.len() <= BLOCK_SIZE);
    assert!(offset % BLOCK_SIZE == 0);

    let mut hasher = Sha256::default();
    hasher.update(&make_identity(block.len(), 0, offset));
    hasher.update(block);
    // Zero fill block up to BLOCK_SIZE. As a special case, if the first data block is completely
    // empty, it is not zero filled.
    if block.len() != BLOCK_SIZE && !(block.is_empty() && offset == 0) {
        hasher.update(&vec![0; BLOCK_SIZE - block.len()]);
    }

    Hash::from(hasher.finish().bytes())
}

/// Compute the merkle hash of a block of hashes.
///
/// Both `hash_block` and `hash_hashes` will zero fill incomplete buffers, but unlike `hash_block`,
/// which includes the actual buffer size in the hash, `hash_hashes` always uses a size of
/// [`BLOCK_SIZE`] when computing the hash. Therefore, the following inputs are equivalent:
/// ```ignore
/// let data_hash = "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
///     .parse()
///     .unwrap();
/// let zero_hash = "0000000000000000000000000000000000000000000000000000000000000000"
///     .parse()
///     .unwrap();
/// let hash_of_single_hash = fuchsia_merkle::hash_hashes(&vec![data_hash], 0, 0);
/// let hash_of_single_hash_and_zero_hash =
///     fuchsia_merkle::hash_hashes(&vec![data_hash, zero_hash], 0, 0);
/// assert_eq!(hash_of_single_hash, hash_of_single_hash_and_zero_hash);
/// ```
///
/// # Panics
///
/// Panics if any of the following conditions are met:
/// - `hashes.len()` is 0
/// - `hashes.len() > HASHES_PER_BLOCK`
/// - `level` is 0
/// - `offset` is not aligned to [`BLOCK_SIZE`]
pub(crate) fn hash_hashes(hashes: &[Hash], level: usize, offset: usize) -> Hash {
    assert_ne!(hashes.len(), 0);
    assert!(hashes.len() <= HASHES_PER_BLOCK);
    assert!(level > 0);
    assert!(offset % BLOCK_SIZE == 0);

    let mut hasher = Sha256::default();
    hasher.update(&make_identity(BLOCK_SIZE, level, offset));
    for ref hash in hashes.iter() {
        hasher.update(hash.as_bytes());
    }
    for _ in 0..(HASHES_PER_BLOCK - hashes.len()) {
        hasher.update(&[0; HASH_SIZE]);
    }

    Hash::from(hasher.finish().bytes())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hash_block_empty() {
        let block = vec![];
        let hash = hash_block(&block[..], 0);
        let expected =
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b".parse().unwrap();
        assert_eq!(hash, expected);
    }

    #[test]
    fn test_hash_block_single() {
        let block = vec![0xFF; 8192];
        let hash = hash_block(&block[..], 0);
        let expected =
            "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737".parse().unwrap();
        assert_eq!(hash, expected);
    }

    #[test]
    fn test_hash_hashes_full_block() {
        let mut leafs = Vec::new();
        {
            let block = vec![0xFF; BLOCK_SIZE];
            for i in 0..HASHES_PER_BLOCK {
                leafs.push(hash_block(&block, i * BLOCK_SIZE));
            }
        }
        let root = hash_hashes(&leafs, 1, 0);
        let expected =
            "1e6e9c870e2fade25b1b0288ac7c216f6fae31c1599c0c57fb7030c15d385a8d".parse().unwrap();
        assert_eq!(root, expected);
    }

    #[test]
    fn test_hash_hashes_zero_pad_same_length() {
        let data_hash =
            "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b".parse().unwrap();
        let zero_hash =
            "0000000000000000000000000000000000000000000000000000000000000000".parse().unwrap();
        let hash_of_single_hash = hash_hashes(&vec![data_hash], 1, 0);
        let hash_of_single_hash_and_zero_hash = hash_hashes(&vec![data_hash, zero_hash], 1, 0);
        assert_eq!(hash_of_single_hash, hash_of_single_hash_and_zero_hash);
    }
}
