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

    #[error("The length of the concatenated path data must fit in a u32")]
    TooMuchPathData,

    #[error("Writing archive")]
    Write(#[source] io::Error),

    #[error("Copying content chunk to archive")]
    Copy(#[source] io::Error),

    #[error("Seeking within archive")]
    Seek(#[source] io::Error),

    #[error("Reading archive")]
    Read(#[source] io::Error),

    #[error("Getting archive size")]
    GetSize(#[source] io::Error),

    #[error(
        "Content chunk had expected size {expected} but Reader supplied {actual} at archive \
             path: {}",
        format_path_for_error(path)
    )]
    ContentChunkSizeMismatch { expected: u64, actual: u64, path: Vec<u8> },

    #[error("Missing directory chunk index entry")]
    MissingDirectoryChunkIndexEntry,

    #[error("Missing directory names chunk index entry")]
    MissingDirectoryNamesChunkIndexEntry,

    #[error("Serialize index")]
    SerializeIndex(#[source] std::io::Error),

    #[error("Serialize directory chunk index entry")]
    SerializeDirectoryChunkIndexEntry(#[source] std::io::Error),

    #[error("Serialize directory names chunk index entry")]
    SerializeDirectoryNamesChunkIndexEntry(#[source] std::io::Error),

    #[error("Serialize directory entry")]
    SerializeDirectoryEntry(#[source] std::io::Error),

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

    #[error(
        "Invalid chunk length for chunk {}, offset + length overflows, offset: {offset}, length: \
         {length}",
        ascii_chunk(chunk_type)
    )]
    InvalidChunkLength { chunk_type: ChunkType, offset: u64, length: u64 },

    #[error("Bad length of directory names chunk, expected multiple of 8, found {0}")]
    InvalidDirectoryNamesChunkLen(u64),

    #[error(
        "Bad length of directory chunk, expected multiple of {}, found {0}",
        DIRECTORY_ENTRY_LEN
    )]
    InvalidDirectoryChunkLen(u64),

    #[error(
        "Unsorted directory entry {entry_index} has name {} but comes after name \
         {}",
        format_path_for_error(name),
        format_path_for_error(previous_name)
    )]
    DirectoryEntriesOutOfOrder { entry_index: usize, name: Vec<u8>, previous_name: Vec<u8> },

    #[error("Directory entry has a zero length name")]
    ZeroLengthName,

    #[error("Directory entry name starts with '/' {}", format_path_for_error(.0))]
    NameStartsWithSlash(Vec<u8>),

    #[error("Directory entry name ends with '/' {}", format_path_for_error(.0))]
    NameEndsWithSlash(Vec<u8>),

    #[error("Directory entry name contains the null character {}", format_path_for_error(.0))]
    NameContainsNull(Vec<u8>),

    #[error(
        "Directory entry name contains an empty segment (consecutive '/') {}",
        format_path_for_error(.0)
    )]
    NameContainsEmptySegment(Vec<u8>),

    #[error("Directory entry name contains a segment of '.' {}", format_path_for_error(.0))]
    NameContainsDotSegment(Vec<u8>),

    #[error("Directory entry name contains a segment of '..' {}", format_path_for_error(.0))]
    NameContainsDotDotSegment(Vec<u8>),

    #[error(
        "Path data for directory entry {entry_index} has offset {offset} larger than \
             directory names chunk size {chunk_size}"
    )]
    PathDataOffsetTooLarge { entry_index: usize, offset: usize, chunk_size: usize },

    #[error(
        "Path data for directory entry {entry_index} has offset {offset} plus length \
             {length} larger than directory names chunk size {chunk_size}"
    )]
    PathDataLengthTooLarge { entry_index: usize, offset: usize, length: u16, chunk_size: usize },

    #[error("Path data not utf8: {path:?}")]
    PathDataInvalidUtf8 { source: std::str::Utf8Error, path: Vec<u8> },

    #[error("Path not present in archive: {}", format_path_for_error(.0))]
    PathNotPresent(Vec<u8>),

    #[error("Attempted to read past the end of a content chunk")]
    ReadPastEnd,

    #[error(
        "Directory entry for {} has a bad content chunk offset, expected {expected} actual \
         {actual}",
        format_path_for_error(name)
    )]
    InvalidContentChunkOffset { name: Vec<u8>, expected: u64, actual: u64 },

    #[error(
        "Directory entry for {} implies a content chunk end that overflows u64, offset: \
         {offset}, length: {length}",
        format_path_for_error(name)
    )]
    ContentChunkEndOverflow { name: Vec<u8>, offset: u64, length: u64 },

    #[error(
        "Archive has {archive_size} bytes, but content chunk (including padding) for {} ends \
         at {lower_bound}",
        format_path_for_error(name)
    )]
    ContentChunkBeyondArchive { name: Vec<u8>, lower_bound: u64, archive_size: u64 },

    #[error(
        "The content chunk for {} is {chunk_size} bytes but the system does not support \
         buffers larger than {} bytes",
        format_path_for_error(name),
        usize::MAX
    )]
    ContentChunkDoesNotFitInMemory { name: Vec<u8>, chunk_size: u64 },
}

// Displays ChunkType as ascii characters if possible, escapes bytes that are out of range.
// Takes an array reference so it can be called with tuple fields in thiserror format strings.
fn ascii_chunk(bytes: &[u8; 8]) -> String {
    let v: Vec<u8> = bytes.iter().copied().flat_map(std::ascii::escape_default).collect();
    String::from_utf8_lossy(&v).into_owned()
}

// Debug formats `path`, first converting it to an &str if it is valid UTF-8.
fn format_path_for_error(path: &[u8]) -> String {
    std::str::from_utf8(path).map(|s| format!("{s:?}")).unwrap_or_else(|_| format!("{path:?}"))
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
