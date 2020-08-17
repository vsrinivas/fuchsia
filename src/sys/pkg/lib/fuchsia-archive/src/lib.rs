// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Reading, Writing and Listing Fuchsia Archives (FAR) Data
//!
//! This crate is a Rust port of the
//! [Go Far package](https://fuchsia.googlesource.com/fuchsia/+/master/garnet/go/src/far/).
//!
//! # Example
//!
//! ```
//! extern crate failure;
//! extern crate fuchsia_archive;
//! extern crate tempfile;
//!
//! use anyhow::Error;
//! use fuchsia_archive::write;
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
//!     let path = test_dir
//!         .path()
//!         .join(file_name)
//!         .to_string_lossy()
//!         .to_string();
//!     let file = fs::File::open(path).unwrap();
//!     path_content_map.insert(file_name, (file.metadata().unwrap().len(), Box::new(file)));
//! }
//! let mut target = Cursor::new(Vec::new());
//! write(&mut target, path_content_map).unwrap();
//!
//! ```

use serde::{Deserialize, Serialize};
use std::mem;

mod error;
pub use error::Error;

mod name;

mod read;
pub use read::{EntryReader, Reader};

mod write;
pub use write::write;

pub const MAGIC_INDEX_VALUE: [u8; 8] = [0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11];

pub type ChunkType = [u8; 8];

pub const DIR_CHUNK_TYPE: ChunkType = *b"DIR-----";
pub const DIR_NAMES_CHUNK_TYPE: ChunkType = *b"DIRNAMES";

#[derive(Serialize, Deserialize, PartialEq, Debug, Copy, Clone)]
#[repr(C)]
struct Index {
    magic: [u8; 8],
    length: u64,
}

const INDEX_LEN: u64 = mem::size_of::<Index>() as u64;

#[derive(Serialize, Deserialize, PartialEq, Debug, Copy, Clone)]
#[repr(C)]
struct IndexEntry {
    chunk_type: ChunkType,
    offset: u64,
    length: u64,
}

const INDEX_ENTRY_LEN: u64 = mem::size_of::<IndexEntry>() as u64;

#[derive(Serialize, Deserialize, PartialEq, Debug, Copy, Clone)]
#[repr(C)]
struct DirectoryEntry {
    name_offset: u32,
    name_length: u16,
    reserved: u16,
    data_offset: u64,
    data_length: u64,
    reserved2: u64,
}

const DIRECTORY_ENTRY_LEN: u64 = mem::size_of::<DirectoryEntry>() as u64;
const CONTENT_ALIGNMENT: u64 = 4096;

// align rounds i up to a multiple of n
fn align(unrounded_value: u64, multiple: u64) -> u64 {
    let rem = unrounded_value.checked_rem(multiple).unwrap();
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
        bincode::{deserialize_from, serialize_into},
        std::io::{Cursor, Seek, SeekFrom},
    };

    pub(crate) fn example_archive() -> Vec<u8> {
        let mut b: Vec<u8> = vec![0; 16384];
        #[cfg_attr(rustfmt, rustfmt_skip)]
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
        let index = Index { magic: MAGIC_INDEX_VALUE, length: 2 * INDEX_ENTRY_LEN as u64 };
        serialize_into(&mut target, &index).unwrap();
        assert_eq!(target.get_ref().len() as u64, INDEX_LEN);
        target.seek(SeekFrom::Start(0)).unwrap();

        let decoded_index: Index = deserialize_from(&mut target).unwrap();
        assert_eq!(index, decoded_index);
    }

    #[test]
    fn test_serialize_deserialize_index_entry() {
        let mut target = Cursor::new(Vec::new());
        let index_entry = IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 999, length: 444 };
        serialize_into(&mut target, &index_entry).unwrap();
        assert_eq!(target.get_ref().len() as u64, INDEX_ENTRY_LEN);
        target.seek(SeekFrom::Start(0)).unwrap();

        let decoded_index_entry: IndexEntry = deserialize_from(&mut target).unwrap();
        assert_eq!(index_entry, decoded_index_entry);
    }

    #[test]
    fn test_serialize_deserialize_directory_entry() {
        let mut target = Cursor::new(Vec::new());
        let index_entry = DirectoryEntry {
            name_offset: 33,
            name_length: 66,
            reserved: 0,
            data_offset: 99,
            data_length: 1011,
            reserved2: 0,
        };
        serialize_into(&mut target, &index_entry).unwrap();
        assert_eq!(target.get_ref().len() as u64, DIRECTORY_ENTRY_LEN);
        target.seek(SeekFrom::Start(0)).unwrap();

        let decoded_index_entry: DirectoryEntry = deserialize_from(&mut target).unwrap();
        assert_eq!(index_entry, decoded_index_entry);
    }

    #[test]
    fn test_struct_sizes() {
        assert_eq!(INDEX_LEN, 8 + 8);
        assert_eq!(INDEX_ENTRY_LEN, 8 + 8 + 8);
        assert_eq!(DIRECTORY_ENTRY_LEN, 4 + 2 + 2 + 8 + 8 + 8);
    }

    #[test]
    fn test_align_values() {
        assert_eq!(align(3, 8), 8);
        assert_eq!(align(13, 8), 16);
        assert_eq!(align(16, 8), 16);
    }

    #[test]
    #[should_panic]
    fn test_align_zero() {
        align(3, 0);
    }
}
