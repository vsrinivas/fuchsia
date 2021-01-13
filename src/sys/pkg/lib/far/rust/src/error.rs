// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{ChunkType, DIRECTORY_ENTRY_LEN, INDEX_ENTRY_LEN},
    std::io,
};

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

    #[error(
        "Content chunk had expected size {expected} but Reader supplied {actual} at archive \
             path '{path:?}'"
    )]
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

    #[error("Bad length of index entries, expected multiple of {}, found {0}", INDEX_ENTRY_LEN)]
    InvalidIndexEntriesLen(u64),

    #[error(
        "Index entries not strictly increasing, {} followed by {}",
        ascii_chunk(prev),
        ascii_chunk(next)
    )]
    IndexEntriesOutOfOrder { prev: ChunkType, next: ChunkType },

    #[error(
        "Invalid chunk offset for chunk {}, expected {expected}, actual {actual}",
        ascii_chunk(chunk_type)
    )]
    InvalidChunkOffset { chunk_type: ChunkType, expected: u64, actual: u64 },

    #[error("Bad length of directory names chunk, expected multiple of 8, found {0}")]
    InvalidDirectoryNamesChunkLen(u64),

    #[error(
        "Bad length of directory chunk, expected multiple of {}, found {0}",
        DIRECTORY_ENTRY_LEN
    )]
    InvalidDirectoryChunkLen(u64),

    #[error(
        "Unsorted directory entry {entry_index} has name {name} but comes after name \
         {previous_name}"
    )]
    DirectoryEntriesOutOfOrder { entry_index: u64, name: String, previous_name: String },

    #[error("Directory entry has a zero length name")]
    ZeroLengthName,

    #[error("Directory entry name starts with '/' {0:?}")]
    NameStartsWithSlash(String),

    #[error("Directory entry name ends with '/' {0:?}")]
    NameEndsWithSlash(String),

    #[error("Directory entry name contains the null character {0:?}")]
    NameContainsNull(String),

    #[error("Directory entry name contains an empty segment (consecutive '/') {0:?}")]
    NameContainsEmptySegment(String),

    #[error("Directory entry name contains a segment of '.' {0:?}")]
    NameContainsDotSegment(String),

    #[error("Directory entry name contains a segment of '..' {0:?}")]
    NameContainsDotDotSegment(String),

    #[error(
        "Path data for directory entry {entry_index} has offset {offset} larger than \
             directory names chunk size {chunk_size}"
    )]
    PathDataOffsetTooLarge { entry_index: u64, offset: usize, chunk_size: usize },

    #[error(
        "Path data for directory entry {entry_index} has offset {offset} plus length \
             {length} larger than directory names chunk size {chunk_size}"
    )]
    PathDataLengthTooLarge { entry_index: u64, offset: usize, length: u16, chunk_size: usize },

    #[error("Path data not utf8")]
    PathDataInvalidUtf8(#[source] std::str::Utf8Error),

    #[error("Path not present in archive: {0:?}")]
    PathNotPresent(String),

    #[error("Attempted to read past the end of a content chunk")]
    ReadPastEnd,

    #[error(
        "Directory entry for {name} has a bad content chunk offset, expected {expected} actual \
         {actual}"
    )]
    InvalidContentChunkOffset { name: String, expected: u64, actual: u64 },

    #[error(
        "Archive has {archive_size} bytes, but content chunk (including padding) for {name} ends \
         at {lower_bound}"
    )]
    ContentChunkBeyondArchive { name: String, lower_bound: u64, archive_size: u64 },
}

// Displays ChunkType as ascii characters if possible, escapes bytes that are out of range.
// Takes an array reference so it can be called with tuple fields in thiserror format strings.
fn ascii_chunk(bytes: &[u8; 8]) -> String {
    let v: Vec<u8> = bytes.iter().copied().flat_map(std::ascii::escape_default).collect();
    String::from_utf8_lossy(&v).into_owned()
}

#[cfg(test)]
mod tests {
    #[test]
    fn ascii_chunk() {
        assert_eq!(
            super::ascii_chunk(&[b'A', 0, b'b', b'\\', 255, b'-', b'_', b'\n']),
            r"A\x00b\\\xff-_\n"
        );
    }
}
