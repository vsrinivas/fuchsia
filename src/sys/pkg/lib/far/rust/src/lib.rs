// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Reading, Writing and Listing Fuchsia Archives (FAR) Data
//!
//! This crate is a Rust port of the
//! [Go Far package](https://fuchsia.googlesource.com/fuchsia/+/HEAD/garnet/go/src/far/).
//!
//! # Example
//!
//! ```
//! use anyhow::Error;
//! use std::collections::BTreeMap;
//! use std::fs;
//! use std::io::{Cursor, Read, Write};
//! use tempfile::TempDir;
//!
//! fn create_test_files(file_names: &[&str]) -> Result<TempDir, Error> {
//!     let tmp_dir = TempDir::new()?;
//!     for file_name in file_names {
//!         let file_path = tmp_dir.path().join(file_name);
//!         let parent_dir = file_path.parent().unwrap();
//!         fs::create_dir_all(&parent_dir)?;
//!         let file_path = tmp_dir.path().join(file_name);
//!         let mut tmp_file = fs::File::create(&file_path)?;
//!         writeln!(tmp_file, "{}", file_name)?;
//!     }
//!     Ok(tmp_dir)
//! }
//!
//! let file_names = ["b", "a", "dir/c"];
//! let test_dir = create_test_files(&file_names).unwrap();
//! let mut path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = BTreeMap::new();
//! for file_name in file_names.iter() {
//!     let file = fs::File::open(test_dir.path().join(file_name)).unwrap();
//!     path_content_map.insert(file_name, (file.metadata().unwrap().len(), Box::new(file)));
//! }
//! let mut result = Vec::new();
//! fuchsia_archive::write(&mut result, path_content_map).unwrap();
//! let result = &result[..];
//!
//! let reader = fuchsia_archive::Reader::new(Cursor::new(result)).unwrap();
//! let entries = reader.list().map(|e| e.path()).collect::<Vec<_>>();
//! assert_eq!(entries, ["a", "b", "dir/c"]);
//! ```

#![warn(clippy::all)]
#![allow(clippy::let_unit_value)]

use zerocopy::{byteorder::LittleEndian, U16, U32, U64};

mod error;
pub use error::Error;

mod name;

mod read;
pub use read::Reader;

mod utf8_reader;
pub use utf8_reader::Utf8Reader;

mod async_read;
pub use async_read::AsyncReader;

mod async_utf8_reader;
pub use async_utf8_reader::AsyncUtf8Reader;

mod write;
pub use write::write;

pub const MAGIC_INDEX_VALUE: [u8; 8] = [0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11];

pub type ChunkType = [u8; 8];

pub const DIR_CHUNK_TYPE: ChunkType = *b"DIR-----";
pub const DIR_NAMES_CHUNK_TYPE: ChunkType = *b"DIRNAMES";

#[derive(PartialEq, Eq, Debug, Clone, Copy, Default, zerocopy::AsBytes, zerocopy::FromBytes)]
#[repr(C)]
struct Index {
    magic: [u8; 8],
    length: U64<LittleEndian>,
}

const INDEX_LEN: u64 = std::mem::size_of::<Index>() as u64;

#[derive(PartialEq, Eq, Debug, Clone, Copy, Default, zerocopy::AsBytes, zerocopy::FromBytes)]
#[repr(C)]
struct IndexEntry {
    chunk_type: ChunkType,
    offset: U64<LittleEndian>,
    length: U64<LittleEndian>,
}

const INDEX_ENTRY_LEN: u64 = std::mem::size_of::<IndexEntry>() as u64;

#[derive(PartialEq, Eq, Debug, Clone, Copy, Default, zerocopy::AsBytes, zerocopy::FromBytes)]
#[repr(C)]
struct DirectoryEntry {
    name_offset: U32<LittleEndian>,
    name_length: U16<LittleEndian>,
    reserved: U16<LittleEndian>,
    data_offset: U64<LittleEndian>,
    data_length: U64<LittleEndian>,
    reserved2: U64<LittleEndian>,
}

const DIRECTORY_ENTRY_LEN: u64 = std::mem::size_of::<DirectoryEntry>() as u64;
const CONTENT_ALIGNMENT: u64 = 4096;

/// An entry in an archive, returned by Reader::list
#[derive(Debug, PartialEq, Eq)]
pub struct Entry<'a> {
    path: &'a [u8],
    offset: u64,
    length: u64,
}

impl<'a> Entry<'a> {
    /// The path of the entry.
    pub fn path(&self) -> &'a [u8] {
        self.path
    }

    /// The offset in bytes of the entry's content chunk.
    pub fn offset(&self) -> u64 {
        self.offset
    }

    /// The length in bytes of the entry's content chunk.
    pub fn length(&self) -> u64 {
        self.length
    }
}

/// An entry in a UTF-8 archive, returned by Reader::list
#[derive(Debug, PartialEq, Eq)]
pub struct Utf8Entry<'a> {
    path: &'a str,
    offset: u64,
    length: u64,
}

impl<'a> Utf8Entry<'a> {
    /// The path of the entry.
    pub fn path(&self) -> &'a str {
        self.path
    }

    /// The offset in bytes of the entry's content chunk.
    pub fn offset(&self) -> u64 {
        self.offset
    }

    /// The length in bytes of the entry's content chunk.
    pub fn length(&self) -> u64 {
        self.length
    }
}

fn validate_directory_entries_and_paths(
    directory_entries: &[DirectoryEntry],
    path_data: &[u8],
    stream_len: u64,
    end_of_last_non_content_chunk: u64,
) -> Result<(), Error> {
    let mut previous_name: Option<&[u8]> = None;
    let mut previous_entry: Option<&DirectoryEntry> = None;
    for (i, entry) in directory_entries.iter().enumerate() {
        let name = validate_name_for_entry(entry, i, path_data, previous_name)?;
        let () = validate_content_chunk(
            entry,
            previous_entry,
            name,
            stream_len,
            end_of_last_non_content_chunk,
        )?;
        previous_name = Some(name);
        previous_entry = Some(entry);
    }
    Ok(())
}

// Obtain name for current directory entry, making sure it is a valid name and lexicographically
// greater than the previous name.
fn validate_name_for_entry<'a>(
    entry: &DirectoryEntry,
    entry_index: usize,
    path_data: &'a [u8],
    previous_name: Option<&[u8]>,
) -> Result<&'a [u8], Error> {
    let offset = entry.name_offset.get().into_usize();
    if offset >= path_data.len() {
        return Err(Error::PathDataOffsetTooLarge {
            entry_index,
            offset,
            chunk_size: path_data.len(),
        });
    }

    let end = offset + usize::from(entry.name_length.get());
    if end > path_data.len() {
        return Err(Error::PathDataLengthTooLarge {
            entry_index,
            offset,
            length: entry.name_length.get(),
            chunk_size: path_data.len(),
        });
    }

    let name = crate::name::validate_name(&path_data[offset..end])?;

    // Directory entries must be strictly increasing by name
    if let Some(previous_name) = previous_name {
        if previous_name >= name {
            return Err(Error::DirectoryEntriesOutOfOrder {
                entry_index,
                previous_name: previous_name.into(),
                name: name.into(),
            });
        }
    }
    Ok(name)
}

fn validate_content_chunk(
    entry: &DirectoryEntry,
    previous_entry: Option<&DirectoryEntry>,
    name: &[u8],
    stream_len: u64,
    end_of_last_non_content_chunk: u64,
) -> Result<(), Error> {
    // Chunks must be non-overlapping and tightly packed
    let expected_offset = if let Some(previous_entry) = previous_entry {
        next_multiple_of(
            previous_entry.data_offset.get() + previous_entry.data_length.get(),
            CONTENT_ALIGNMENT,
        )
    } else {
        next_multiple_of(end_of_last_non_content_chunk, CONTENT_ALIGNMENT)
    };
    if entry.data_offset.get() != expected_offset {
        return Err(Error::InvalidContentChunkOffset {
            name: name.into(),
            expected: expected_offset,
            actual: entry.data_offset.get(),
        });
    }

    // Chunks must be contained in the archive
    let stream_len_lower_bound = next_multiple_of(
        entry.data_offset.get().checked_add(entry.data_length.get()).ok_or_else(|| {
            Error::ContentChunkEndOverflow {
                name: name.into(),
                offset: entry.data_offset.get(),
                length: entry.data_length.get(),
            }
        })?,
        CONTENT_ALIGNMENT,
    );
    if stream_len_lower_bound > stream_len {
        return Err(Error::ContentChunkBeyondArchive {
            name: name.into(),
            lower_bound: stream_len_lower_bound,
            archive_size: stream_len,
        });
    }
    Ok(())
}

// Return an iterator over the items in an archive.
fn list<'a>(
    directory_entries: &'a [DirectoryEntry],
    path_data: &'a [u8],
) -> impl ExactSizeIterator<Item = Entry<'a>> {
    directory_entries.iter().map(|e| Entry {
        path: &path_data[e.name_offset.get().into_usize()..][..usize::from(e.name_length.get())],
        offset: e.data_offset.get(),
        length: e.data_length.get(),
    })
}

// Returns the directory entry with path `target_path`, or an error if there is not one.
// O(log(# directory entries))
fn find_directory_entry<'a>(
    directory_entries: &'a [DirectoryEntry],
    path_data: &'_ [u8],
    target_path: &'_ [u8],
) -> Result<&'a DirectoryEntry, Error> {
    // FAR spec requires, and [Async]Reader::new enforces, that directory entries are sorted by
    // path data
    // https://fuchsia.dev/fuchsia-src/development/source_code/archive_format?hl=en#directory_chunk_type_dir-----
    let i = directory_entries
        .binary_search_by_key(&target_path, |e| {
            &path_data[e.name_offset.get().into_usize()..][..usize::from(e.name_length.get())]
        })
        .map_err(|_| Error::PathNotPresent(target_path.into()))?;
    Ok(directory_entries.get(i).expect("binary_search on success returns in-bounds index"))
}

trait SafeIntegerConversion {
    fn into_usize(self) -> usize;
}

impl SafeIntegerConversion for u32 {
    fn into_usize(self) -> usize {
        static_assertions::const_assert!(
            std::mem::size_of::<u32>() <= std::mem::size_of::<usize>()
        );
        self as usize
    }
}

// Returns the least multiple of `multiple` that is greater than or equal to `unrounded_value`.
// Panics if `multiple` is zero.
// TODO(fxbug.dev/103981) Replace next_multiple_of with std methods once available.
fn next_multiple_of(unrounded_value: u64, multiple: u64) -> u64 {
    let rem = unrounded_value.checked_rem(multiple).expect("never called with multiple = 0");
    if rem > 0 {
        unrounded_value - rem + multiple
    } else {
        unrounded_value
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use {
        super::*,
        std::io::{Cursor, Read as _, Seek as _, SeekFrom, Write as _},
        zerocopy::AsBytes as _,
    };

    pub(crate) fn example_archive() -> Vec<u8> {
        let mut b: Vec<u8> = vec![0; 16384];
        #[rustfmt::skip]
        let header = vec![
            /* magic */
            0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11,
            /* length of index entries */
            0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* index entry for directory chunk */
            /* chunk type */
            0x44, 0x49, 0x52, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d,
            /* offset to chunk */
            0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* length of chunk */
            0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* index entry for directory names chunk */
            /* chunk type */
            0x44, 0x49, 0x52, 0x4e, 0x41, 0x4d, 0x45, 0x53,
            /* offset to chunk */
            0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* length of chunk */
            0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* directory chunk */
            /* directory table entry for path "a" */
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* directory table entry for path "b" */
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* directory table entry for path "dir/c" */
            0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
            0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* directory names chunk with one byte of padding */
            b'a', b'b', b'd', b'i', b'r', b'/', b'c', 0x00,
        ];
        b[0..header.len()].copy_from_slice(header.as_slice());
        let content_a = b"a\n";
        let a_loc = 4096;
        b[a_loc..a_loc + content_a.len()].copy_from_slice(content_a);
        let content_b = b"b\n";
        let b_loc = 8192;
        b[b_loc..b_loc + content_b.len()].copy_from_slice(content_b);
        let content_c = b"dir/c\n";
        let c_loc = 12288;
        b[c_loc..c_loc + content_c.len()].copy_from_slice(content_c);
        b
    }

    #[test]
    fn test_serialize_deserialize_index() {
        let mut target = Cursor::new(Vec::new());
        let index = Index { magic: MAGIC_INDEX_VALUE, length: (2 * INDEX_ENTRY_LEN as u64).into() };
        let () = target.write_all(index.as_bytes()).unwrap();
        assert_eq!(target.get_ref().len() as u64, INDEX_LEN);
        assert_eq!(target.seek(SeekFrom::Start(0)).unwrap(), 0);

        let mut decoded_index = Index::default();
        let () = target.get_ref().as_slice().read_exact(decoded_index.as_bytes_mut()).unwrap();
        assert_eq!(index, decoded_index);
    }

    #[test]
    fn test_serialize_deserialize_index_entry() {
        let mut target = Cursor::new(Vec::new());
        let index_entry =
            IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 999.into(), length: 444.into() };
        let () = target.write_all(index_entry.as_bytes()).unwrap();
        assert_eq!(target.get_ref().len() as u64, INDEX_ENTRY_LEN);
        assert_eq!(target.seek(SeekFrom::Start(0)).unwrap(), 0);

        let mut decoded_index_entry = IndexEntry::default();
        let () =
            target.get_ref().as_slice().read_exact(decoded_index_entry.as_bytes_mut()).unwrap();
        assert_eq!(index_entry, decoded_index_entry);
    }

    #[test]
    fn test_serialize_deserialize_directory_entry() {
        let mut target = Cursor::new(Vec::new());
        let directory_entry = DirectoryEntry {
            name_offset: 33.into(),
            name_length: 66.into(),
            reserved: 0.into(),
            data_offset: 99.into(),
            data_length: 1011.into(),
            reserved2: 0.into(),
        };
        let () = target.write_all(directory_entry.as_bytes()).unwrap();
        assert_eq!(target.get_ref().len() as u64, DIRECTORY_ENTRY_LEN);
        assert_eq!(target.seek(SeekFrom::Start(0)).unwrap(), 0);

        let mut decoded_directory_entry = DirectoryEntry::default();
        let () =
            target.get_ref().as_slice().read_exact(decoded_directory_entry.as_bytes_mut()).unwrap();
        assert_eq!(directory_entry, decoded_directory_entry);
    }

    #[test]
    fn test_struct_sizes() {
        assert_eq!(INDEX_LEN, 8 + 8);
        assert_eq!(INDEX_ENTRY_LEN, 8 + 8 + 8);
        assert_eq!(DIRECTORY_ENTRY_LEN, 4 + 2 + 2 + 8 + 8 + 8);
    }

    #[test]
    fn into_usize_no_panic() {
        assert_eq!(u32::MAX.into_usize(), u32::MAX.try_into().unwrap());
    }

    #[test]
    fn test_next_multiple_of() {
        assert_eq!(next_multiple_of(3, 8), 8);
        assert_eq!(next_multiple_of(13, 8), 16);
        assert_eq!(next_multiple_of(16, 8), 16);
    }

    #[test]
    #[should_panic]
    fn test_next_multiple_of_zero() {
        next_multiple_of(3, 0);
    }
}
