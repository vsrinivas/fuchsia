// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ChunkType, std::io};

#[non_exhaustive]
#[derive(thiserror::Error, Debug)]
pub enum Error {
    #[error("Names can be no longer than 65,535 bytes, supplied name was {0} bytes")]
    NameTooLong(usize),

    #[error("Writing archive")]
    Write(#[source] io::Error),

    #[error("Copying content chunk to archive")]
    Copy(#[source] io::Error),

    #[error("Seeking within archive")]
    Seek(#[source] io::Error),

    #[error("Reading archive")]
    Read(#[source] io::Error),

    #[error("Content chunk had expected size {expected} but Reader supplied {actual} at archive path '{path:?}'")]
    ContentChunkSizeMismatch { expected: u64, actual: u64, path: String },

    #[error("Missing directory chunk index entry")]
    MissingDirectoryChunkIndexEntry,

    #[error("Missing directory names chunk index entry")]
    MissingDirectoryNamesChunkIndexEntry,

    #[error("Deserializing directory entry")]
    DeserializeDirectoryEntry(#[source] bincode::Error),

    #[error("Deserializing index")]
    DeserializeIndex(#[source] bincode::Error),

    #[error("Deserializing index entry")]
    DeserializeIndexEntry(#[source] bincode::Error),

    #[error("Serialize index")]
    SerializeIndex(#[source] bincode::Error),

    #[error("Serialize directory chunk index entry")]
    SerializeDirectoryChunkIndexEntry(#[source] bincode::Error),

    #[error("Serialize directory names chunk index entry")]
    SerializeDirectoryNamesChunkIndexEntry(#[source] bincode::Error),

    #[error("Serialize directory entry")]
    SerializeDirectoryEntry(#[source] bincode::Error),

    #[error("Invalid magic bytes, expected [c8, bf, 0b, 48, ad, ab, c5, 11], found {0:02x?}")]
    InvalidMagic([u8; 8]),

    #[error(
        "Bad length of index entries, expected multiple of {}, found {0}",
        crate::INDEX_ENTRY_LEN
    )]
    InvalidIndexEntriesLen(u64),

    #[error("Index entries out of order, {:02x?} came before {:02x?}", prev.to_be_bytes(), next.to_be_bytes())]
    IndexEntriesOutOfOrder { prev: ChunkType, next: ChunkType },

    #[error("Path data not utf8")]
    PathDataInvalidUtf8(#[source] std::str::Utf8Error),

    #[error("Chunk type {:02x?} overlaps with index chunk", .0.to_be_bytes())]
    ChunkOverlapsIndex(ChunkType),

    #[error("Invalid chunk type {:02x?}", .0.to_be_bytes())]
    InvalidChunkType(ChunkType),

    #[error("Path not present in archive: {0:?}")]
    PathNotPresent(String),

    #[error("Attempted to read past the end of a content chunk")]
    ReadPastEnd,
}
