// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fuchsia_merkle` contains types and methods for building and working with merkle trees.

#![deny(missing_docs)]
#![warn(clippy::all)]

use {
    futures::{AsyncRead, AsyncReadExt as _},
    std::io::{self, Read},
};

pub use fuchsia_hash::{Hash, HASH_SIZE};

/// The size of a single block of data (or hashes), in bytes.
pub const BLOCK_SIZE: usize = 8192;

mod util;

mod tree;
pub use crate::tree::MerkleTree;

mod builder;
pub use crate::builder::MerkleTreeBuilder;

mod writer;
pub use crate::writer::MerkleTreeWriter;

/// Compute a merkle tree from a `&[u8]`.
pub fn from_slice(slice: &[u8]) -> MerkleTree {
    let mut builder = MerkleTreeBuilder::new();
    builder.write(slice);
    builder.finish()
}

/// Compute a merkle tree from a `std::io::Read`.
pub fn from_read<R>(reader: &mut R) -> Result<MerkleTree, io::Error>
where
    R: Read,
{
    let mut buf = [0; BLOCK_SIZE];
    let mut builder = MerkleTreeBuilder::new();

    loop {
        let len = reader.read(&mut buf)?;
        if len == 0 {
            break;
        }
        builder.write(&buf[0..len]);
    }

    Ok(builder.finish())
}

/// Compute a merkle tree from a `futures::io::AsyncRead`.
pub async fn from_async_read<R>(reader: &mut R) -> Result<MerkleTree, io::Error>
where
    R: AsyncRead + Unpin,
{
    let mut buf = [0; BLOCK_SIZE];
    let mut builder = MerkleTreeBuilder::new();

    loop {
        let len = reader.read(&mut buf).await?;
        if len == 0 {
            break;
        }
        builder.write(&buf[0..len]);
    }

    Ok(builder.finish())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_from_slice() {
        let file = b"hello world";
        let mut builder = MerkleTreeBuilder::new();
        builder.write(&file[..]);
        let expected = builder.finish();

        let actual = from_slice(&file[..]);
        assert_eq!(expected, actual);
    }

    #[test]
    fn test_from_read() {
        let file = b"hello world";
        let mut builder = MerkleTreeBuilder::new();
        builder.write(&file[..]);
        let expected = builder.finish();

        let actual = from_read(&mut &file[..]).unwrap();
        assert_eq!(expected, actual);
    }

    #[test]
    fn test_from_async_read() {
        futures::executor::block_on(async {
            let file = b"hello world";
            let mut builder = MerkleTreeBuilder::new();
            builder.write(&file[..]);
            let expected = builder.finish();

            let actual = from_async_read(&mut &file[..]).await.unwrap();
            assert_eq!(expected, actual);
        })
    }
}
