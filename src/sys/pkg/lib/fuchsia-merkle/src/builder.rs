// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::cmp::min;

use crate::tree::MerkleTree;
use crate::util::{hash_block, hash_hashes, HASHES_PER_BLOCK};
use crate::{Hash, BLOCK_SIZE};

/// A `MerkleTreeBuilder` generates a [`MerkleTree`] from one or more write calls.
///
/// # Examples
/// ```
/// # use fuchsia_merkle::*;
/// let data = vec![0xff; 8192];
/// let mut builder = MerkleTreeBuilder::new();
/// for i in 0..8 {
///     builder.write(&data[..]);
/// }
/// assert_eq!(
///     builder.finish().root(),
///     "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf"
///         .parse()
///         .unwrap()
/// );
/// ```
#[derive(Clone, Debug)]
pub struct MerkleTreeBuilder {
    /// Buffer to hold a partial block of data between [`MerkleTreeBuilder::write`] calls.
    /// `block.len()` will never exceed [`BLOCK_SIZE`].
    block: Vec<u8>,
    levels: Vec<Vec<Hash>>,
}

impl Default for MerkleTreeBuilder {
    fn default() -> Self {
        Self { levels: vec![Vec::new()], block: Vec::with_capacity(BLOCK_SIZE) }
    }
}

impl MerkleTreeBuilder {
    /// Creates a new, empty `MerkleTreeBuilder`.
    pub fn new() -> Self {
        Self::default()
    }

    /// Append a buffer of bytes to the merkle tree.
    ///
    /// No internal buffering is required if all writes are [`BLOCK_SIZE`] aligned.
    pub fn write(&mut self, buf: &[u8]) {
        // Fill the current partial block, if it exists.
        let buf = if self.block.is_empty() {
            buf
        } else {
            let left = BLOCK_SIZE - self.block.len();
            let prefix = min(buf.len(), left);
            let (buf, rest) = buf.split_at(prefix);
            self.block.extend_from_slice(buf);
            if self.block.len() == BLOCK_SIZE {
                self.push_data_hash(self.hash_block(&self.block[..]));
            }
            rest
        };

        // Write full blocks, saving any final partial block for later writes.
        for block in buf.chunks(BLOCK_SIZE) {
            if block.len() == BLOCK_SIZE {
                self.push_data_hash(self.hash_block(block));
            } else {
                self.block.extend_from_slice(block);
            }
        }
    }

    /// Hash a block of data (level 0), using an offset based on the current number of level 0
    /// hashes.
    fn hash_block(&self, block: &[u8]) -> Hash {
        hash_block(block, self.levels[0].len() * BLOCK_SIZE)
    }

    /// Save a data block hash, propagating full blocks of hashes to higher layers. Also clear a
    /// stored data block.
    fn push_data_hash(&mut self, hash: Hash) {
        self.block.clear();
        self.levels[0].push(hash);
        if self.levels[0].len() % HASHES_PER_BLOCK == 0 {
            self.commit_tail_block(0);
        }
    }

    /// Hash a complete (or final partial) block of hashes, chaining to higher levels as needed.
    fn commit_tail_block(&mut self, level: usize) {
        let len = self.levels[level].len();
        let next_level = level + 1;

        if next_level >= self.levels.len() {
            self.levels.push(Vec::new());
        }

        let first_hash = if len % HASHES_PER_BLOCK == 0 {
            len - HASHES_PER_BLOCK
        } else {
            len - (len % HASHES_PER_BLOCK)
        };

        let hash = hash_hashes(
            &self.levels[level][first_hash..],
            next_level,
            self.levels[next_level].len() * BLOCK_SIZE,
        );

        self.levels[next_level].push(hash);
        if self.levels[next_level].len() % HASHES_PER_BLOCK == 0 {
            self.commit_tail_block(next_level);
        }
    }

    /// Finalize all levels of the merkle tree, converting this `MerkleTreeBuilder` instance to a
    /// [`MerkleTree`].
    pub fn finish(mut self) -> MerkleTree {
        // The data protected by the tree may not be BLOCK_SIZE aligned. Commit a partial data
        // block before finalizing the hash levels.
        // Also, an empty tree consists of a single, empty block. Handle that case now as well.
        if !self.block.is_empty() || self.levels[0].is_empty() {
            self.push_data_hash(self.hash_block(&self.block[..]));
        }

        // Enumerate the hash levels, finalizing any that have a partial block of hashes.
        // `commit_tail_block` may add new levels to the tree, so don't assume a length up front.
        for level in 0.. {
            if level >= self.levels.len() {
                break;
            }

            let len = self.levels[level].len();
            if len > 1 && len % HASHES_PER_BLOCK != 0 {
                self.commit_tail_block(level);
            }
        }

        MerkleTree::from_levels(self.levels)
    }
}

impl From<MerkleTreeBuilder> for MerkleTree {
    fn from(builder: MerkleTreeBuilder) -> Self {
        builder.finish()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::cmp::min;

    macro_rules! test_case {
        ($name:ident, $input:expr, $output:expr) => {
            #[test]
            fn $name() {
                let input = $input;
                let mut tree = MerkleTreeBuilder::new();
                tree.write(input.as_slice());
                let actual = tree.finish().root();
                let expected: Hash = $output.parse().unwrap();
                assert_eq!(expected, actual);
            }
        };
    }

    test_case!(
        test_empty,
        vec![],
        "15ec7bf0b50732b49f8228e07d24365338f9e3ab994b00af08e5a3bffe55fd8b"
    );

    test_case!(
        test_oneblock,
        vec![0xFF; 8192],
        "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737"
    );

    test_case!(
        test_small,
        vec![0xFF; 65536],
        "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf"
    );

    test_case!(
        test_large,
        vec![0xFF; 2105344],
        "7d75dfb18bfd48e03b5be4e8e9aeea2f89880cb81c1551df855e0d0a0cc59a67"
    );

    test_case!(
        test_unaligned,
        vec![0xFF; 2109440],
        "7577266aa98ce587922fdc668c186e27f3c742fb1b732737153b70ae46973e43"
    );

    #[test]
    fn test_unaligned_single_block() {
        let data = vec![0xFF; 8192];
        let mut tree = MerkleTreeBuilder::new();
        let (first, second) = &data[..].split_at(1024);
        tree.write(first);
        tree.write(second);

        let root = tree.finish().root();

        let expected =
            "68d131bc271f9c192d4f6dcd8fe61bef90004856da19d0f2f514a7f4098b0737".parse().unwrap();
        assert_eq!(root, expected);
    }

    #[test]
    fn test_unaligned_n_block() {
        let data = vec![0xFF; 65536];
        let expected =
            "f75f59a944d2433bc6830ec243bfefa457704d2aed12f30539cd4f18bf1d62cf".parse().unwrap();

        for chunk_size in vec![1, 100, 1024, 8193] {
            let mut tree = MerkleTreeBuilder::new();
            for block in data.as_slice().chunks(chunk_size) {
                tree.write(block);
            }
            let root = tree.finish().root();

            assert_eq!(root, expected);
        }
    }

    #[test]
    fn test_fuchsia() {
        let fuchsia: Vec<_> =
            vec![0xff, 0x00, 0x80].into_iter().cycle().take(3 * BLOCK_SIZE).collect();

        let mut t = MerkleTreeBuilder::new();

        let mut remaining = 0xff0080;
        while remaining > 0 {
            let n = min(remaining, fuchsia.len());
            t.write(&fuchsia[..n]);
            remaining -= n;
        }
        let actual = t.finish().root();
        let expected: Hash =
            "2feb488cffc976061998ac90ce7292241dfa86883c0edc279433b5c4370d0f30".parse().unwrap();
        assert_eq!(expected, actual);
    }
}
