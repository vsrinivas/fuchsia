// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::hash::Hash;

/// A `MerkleTree` contains levels of hashes that can be used to verify the integrity of data.
///
/// While a single hash could be used to integrity check some data, if the data (or hash) is
/// corrupt, a single hash can not determine what part of the data is corrupt. A `MerkleTree`,
/// however, contains a hash for every 8K block of data, allowing it to identify which 8K blocks of
/// data are corrupt. A `MerkleTree` also allows individual 8K blocks of data to be verified
/// without having to verify the rest of the data.
///
/// Furthermore, a `MerkleTree` contains multiple levels of hashes, where each level
/// contains hashes of 8K blocks of hashes of the lower level. The top level always contains a
/// single hash, the merkle root. This tree of hashes allows a `MerkleTree` to determine which of
/// its own hashes are corrupt, if any.
///
/// # Structure Details
///
/// A merkle tree contains levels. A level is a row of the tree, starting at 0 and counting upward.
/// Level 0 represents the leaves of the tree which contain hashes of chunks of the input stream.
///
/// Each level consists of a hash for each 8K block of hashes from the previous level (or, for
/// level 0, each 8K block of data). When computing a hash, the 8K block is prepended with a block
/// identity.
///
/// A block identity is the binary OR of the starting byte index of the block within
/// the current level and the current level index, followed by the length of the block. For level
/// 0, the length of the block is 8K, except for the last block, which may be less than 8K. All
/// other levels use a block length of 8K.
#[derive(Clone, Eq, PartialEq, Ord, PartialOrd, Hash, Debug)]
pub struct MerkleTree {
    levels: Vec<Vec<Hash>>,
}

impl MerkleTree {
    /// Creates a `MerkleTree` from a well-formed tree of hashes.
    ///
    /// A tree of hashes is well-formed iff:
    /// - The length of the last level is 1.
    /// - The length of every hash level is the length of the prior hash level divided by
    ///   `HASHES_PER_BLOCK`, rounded up to the nearest integer.
    pub(crate) fn from_levels(levels: Vec<Vec<Hash>>) -> MerkleTree {
        MerkleTree { levels: levels }
    }

    /// The root hash of the merkle tree.
    pub fn root(&self) -> Hash {
        self.levels[self.levels.len() - 1][0]
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::util::HASHES_PER_BLOCK;
    use crate::util::{hash_block, hash_hashes};
    use crate::BLOCK_SIZE;

    impl MerkleTree {
        /// Given the index of a block of data, lookup its hash.
        fn leaf_hash(&self, block: usize) -> Hash {
            self.levels[0][block]
        }
    }

    #[test]
    fn test_single_full_hash_block() {
        let mut leafs = Vec::new();
        {
            let block = vec![0xFF; BLOCK_SIZE];
            for i in 0..HASHES_PER_BLOCK {
                leafs.push(hash_block(&block, i * BLOCK_SIZE));
            }
        }
        let root = hash_hashes(&leafs, 1, 0);

        let tree = MerkleTree::from_levels(vec![leafs.clone(), vec![root]]);

        assert_eq!(tree.root(), root);
        for i in 0..HASHES_PER_BLOCK {
            assert_eq!(tree.leaf_hash(i), leafs[i]);
        }
    }
}
