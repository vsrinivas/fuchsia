#![allow(dead_code, unused, unused_mut)]

//! # Reading, Writing and Listing Fuchsia Archives (FAR) Data
//!
//! This crate is a Rust port of the
//! [Go Far package](https://fuchsia.googlesource.com/garnet/+/master/go/src/far/).
//!
//! # Example
//!
//! ```
//! extern crate failure;
//! extern crate fuchsia_archive;
//! extern crate tempdir;
//!
//! use failure::Error;
//! use fuchsia_archive::write;
//! use std::collections::HashMap;
//! use std::fs::create_dir_all;
//! use std::fs::File;
//! use std::io::{Cursor, Write};
//! use tempdir::TempDir;
//!
//! fn create_test_files(file_names: &[&str]) -> Result<TempDir, Error> {
//!     let tmp_dir = TempDir::new("fuchsia_archive_test")?;
//!     for file_name in file_names {
//!         let file_path = tmp_dir.path().join(file_name);
//!         let parent_dir = file_path.parent().unwrap();
//!         create_dir_all(&parent_dir)?;
//!         let file_path = tmp_dir.path().join(file_name);
//!         let mut tmp_file = File::create(&file_path)?;
//!         writeln!(tmp_file, "{}", file_name)?;
//!     }
//!     Ok(tmp_dir)
//! }
//!
//! let files = ["b", "a", "dir/c"];
//! let test_dir = create_test_files(&files).unwrap();
//! let mut inputs: HashMap<String, String> = HashMap::new();
//! for file_name in files.iter() {
//!     let path = test_dir
//!         .path()
//!         .join(file_name)
//!         .to_string_lossy()
//!         .to_string();
//!     inputs.insert(file_name.to_string(), path);
//! }
//! let mut target = Cursor::new(Vec::new());
//! write(
//!     &mut target,
//!     &mut inputs.iter().map(|(a, b)| (a.as_str(), b.as_str())),
//! ).unwrap();
//!
//! ```

extern crate bincode;
#[macro_use]
extern crate failure;
#[cfg(test)]
extern crate itertools;
extern crate serde;
#[macro_use]
extern crate serde_derive;
extern crate tempdir;

use bincode::{deserialize_from, serialize_into, Infinite};
use failure::Error;
use std::collections::BTreeMap;
use std::fs;
use std::fs::File;
use std::io::{copy, Cursor, Read, Seek, SeekFrom, Write};
use std::str;

const MAGIC_INDEX_VALUE: [u8; 8] = [0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11];

type ChunkType = u64;

const HASH_CHUNK: ChunkType = 0;
const DIR_HASH_CHUNK: ChunkType = 0x2d_48_53_41_48_52_49_44; // "DIRHASH-"
const DIR_CHUNK: ChunkType = 0x2d_2d_2d_2d_2d_52_49_44; // "DIR-----"
const DIR_NAMES_CHUNK: ChunkType = 0x53_45_4d_41_4e_52_49_44; // "DIRNAMES"

#[derive(Serialize, Deserialize, PartialEq, Debug)]
struct Index {
    magic: [u8; 8],
    length: u64,
}

const INDEX_LEN: u64 = 8 + 8;

#[derive(Serialize, Deserialize, PartialEq, Debug)]
struct IndexEntry {
    chunk_type: ChunkType,
    offset: u64,
    length: u64,
}

const INDEX_ENTRY_LEN: u64 = 8 + 8 + 8;

#[derive(Serialize, Deserialize, PartialEq, Debug, Clone)]
struct DirectoryEntry {
    name_offset: u32,
    name_length: u16,
    reserved: u16,
    data_offset: u64,
    data_length: u64,
    reserved2: u64,
}

const DIRECTORY_ENTRY_LEN: u64 = 4 + 2 + 2 + 8 + 8 + 8;
const CONTENT_ALIGNMENT: u64 = 4096;

fn write_zeros<T>(target: &mut T, count: usize) -> Result<(), Error>
where
    T: Write,
{
    let b = vec![0u8; count];
    target.write_all(&b)?;
    Ok(())
}

/// Write a FAR-formatted archive to the target.
pub fn write<T>(target: &mut T, inputs: &mut Iterator<Item = (&str, &str)>) -> Result<(), Error>
where
    T: Write,
{
    let input_map: BTreeMap<&str, &str> = inputs.collect();
    let mut path_data: Vec<u8> = vec![];
    let mut entries = vec![];
    for (destination_name, source_name) in &input_map {
        let metadata = fs::metadata(source_name)?;
        if destination_name.len() > u16::max_value() as usize {
            return Err(format_err!("Destination name is too long"));
        }
        entries.push(DirectoryEntry {
            name_offset: path_data.len() as u32,
            name_length: destination_name.len() as u16,
            reserved: 0,
            data_offset: 0,
            data_length: metadata.len(),
            reserved2: 0,
        });
        path_data.extend_from_slice(destination_name.as_bytes());
    }

    let index = Index {
        magic: MAGIC_INDEX_VALUE,
        length: 2 * INDEX_ENTRY_LEN as u64,
    };

    let dir_index = IndexEntry {
        chunk_type: DIR_CHUNK as u64,
        offset: INDEX_LEN + INDEX_ENTRY_LEN * 2,
        length: entries.len() as u64 * DIRECTORY_ENTRY_LEN,
    };

    let name_index = IndexEntry {
        chunk_type: DIR_NAMES_CHUNK as u64,
        offset: dir_index.offset + dir_index.length,
        length: align(path_data.len() as u64, 8),
    };

    serialize_into(target, &index, Infinite)?;

    serialize_into(target, &dir_index, Infinite)?;

    serialize_into(target, &name_index, Infinite)?;

    let mut content_offset = align(name_index.offset + name_index.length, CONTENT_ALIGNMENT);

    for entry in &mut entries {
        entry.data_offset = content_offset;
        content_offset = align(content_offset + entry.data_length, CONTENT_ALIGNMENT);
        serialize_into(target, &entry, Infinite)?;
    }

    target.write_all(&path_data)?;

    write_zeros(target, name_index.length as usize - path_data.len())?;

    let pos = name_index.offset + name_index.length;
    let padding_count = align(pos, CONTENT_ALIGNMENT) - pos;
    write_zeros(target, padding_count as usize)?;

    for (entry_index, source_name) in input_map.values().enumerate() {
        let mut f = File::open(source_name)?;
        copy(&mut f, target)?;
        let pos = entries[entry_index].data_offset + entries[entry_index].data_length;
        let padding_count = align(pos, CONTENT_ALIGNMENT) - pos;
        write_zeros(target, padding_count as usize)?;
    }

    Ok(())
}

/// A struct to open and read FAR-formatted archive.
pub struct Reader<'a, T: 'a>
where
    T: Read + Seek,
{
    source: &'a mut T,
    dir_index: IndexEntry,
    dir_name_index: IndexEntry,
    directory_entries: Vec<DirectoryEntry>,
    path_data: Vec<u8>,
}

impl<'a, T> Reader<'a, T>
where
    T: Read + Seek,
{
    /// Create a new Reader for the provided source.
    pub fn new(source: &'a mut T) -> Result<Reader<'a, T>, Error> {
        let index = Reader::<T>::read_index(source)?;

        let (dir_index, dir_name_index) =
            Reader::<T>::read_index_entries(source, index.length / INDEX_ENTRY_LEN, &index)?;
        if dir_index.is_none() {
            return Err(format_err!("Invalid archive, missing directory index"));
        }
        let dir_index = dir_index.unwrap();

        if dir_name_index.is_none() {
            return Err(format_err!("Invalid archive, missing directory name index"));
        }
        let dir_name_index = dir_name_index.unwrap();

        let dir_entry_count = dir_index.length / DIRECTORY_ENTRY_LEN;
        let mut dir_entires = vec![];
        for i in 0..dir_entry_count {
            dir_entires.push(deserialize_from(source, Infinite)?);
        }

        source.seek(SeekFrom::Start(dir_name_index.offset))?;
        let mut path_data = vec![0; dir_name_index.length as usize];
        source.read_exact(&mut path_data)?;

        Ok(Reader {
            source,
            dir_index,
            dir_name_index,
            directory_entries: dir_entires,
            path_data,
        })
    }

    /// Return a list of the items in the archive
    pub fn list(&self) -> Result<Vec<String>, Error> {
        let mut file_names = vec![];
        for entry in &self.directory_entries {
            let name_start = entry.name_offset as usize;
            let after_name_end = name_start + entry.name_length as usize;
            let file_name_str = str::from_utf8(&self.path_data[name_start..after_name_end])?;
            let file_name = String::from(file_name_str);
            file_names.push(file_name);
        }
        Ok(file_names)
    }

    fn read_index(source: &mut T) -> Result<Index, Error> {
        let decoded_index: Index = deserialize_from(source, Infinite)?;
        if decoded_index.magic != MAGIC_INDEX_VALUE {
            Err(format_err!("Invalid archive, bad magic"))
        } else if decoded_index.length % INDEX_ENTRY_LEN != 0 {
            Err(format_err!("Invalid archive, bad index length"))
        } else {
            Ok(decoded_index)
        }
    }

    fn read_index_entries(
        source: &mut T, count: u64, index: &Index,
    ) -> Result<(Option<IndexEntry>, Option<IndexEntry>), Error> {
        let mut dir_index: Option<IndexEntry> = None;
        let mut dir_name_index: Option<IndexEntry> = None;
        let mut last_chunk_type: Option<ChunkType> = None;
        for i in 0..count {
            let entry: IndexEntry = deserialize_from(source, Infinite)?;

            match last_chunk_type {
                None => {}
                Some(chunk_type) => {
                    if chunk_type > entry.chunk_type {
                        return Err(format_err!("Invalid archive, invalid index entry order"));
                    }
                }
            }

            last_chunk_type = Some(entry.chunk_type);

            if entry.offset < index.length {
                return Err(format_err!("Invalid archive, short offset"));
            }

            match entry.chunk_type {
                DIR_HASH_CHUNK => {}
                DIR_NAMES_CHUNK => {
                    dir_name_index = Some(entry);
                }
                DIR_CHUNK => {
                    dir_index = Some(entry);
                }
                _ => {
                    return Err(format_err!("Invalid archive, invalid chunk type"));
                }
            }
        }
        Ok((dir_index, dir_name_index))
    }

    fn find_directory_entry(&self, archive_path: &str) -> Result<DirectoryEntry, Error> {
        for entry in &self.directory_entries {
            let name_start = entry.name_offset as usize;
            let after_name_end = name_start + entry.name_length as usize;
            let file_name_str = str::from_utf8(&self.path_data[name_start..after_name_end])?;
            if file_name_str == archive_path {
                return Ok(entry.clone());
            }
        }
        Err(format_err!("Path '{}' not found in archive", archive_path))
    }

    /// Create an EntryReader for an entry with the specified name.
    pub fn open(&mut self, archive_path: &str) -> Result<EntryReader<T>, Error> {
        let directory_entry = self.find_directory_entry(archive_path)?;

        Ok(EntryReader {
            source: self.source,
            offset: directory_entry.data_offset,
            length: directory_entry.data_length,
        })
    }

    /// Read the entire contents of an entry with the specified name.
    pub fn read_file(&mut self, archive_path: &str) -> Result<Vec<u8>, Error> {
        let mut reader = self.open(archive_path)?;
        reader.read_at(0)
    }

    /// Get the size in bytes of an entry with the specified name.
    pub fn get_size(&mut self, archive_path: &str) -> Result<u64, Error> {
        let directory_entry = self.find_directory_entry(archive_path)?;
        Ok(directory_entry.data_length)
    }
}

/// A structure that allows reading from the offset of an item in a
/// FAR archive.
pub struct EntryReader<'a, T: 'a>
where
    T: Read + Seek,
{
    offset: u64,
    length: u64,
    source: &'a mut T,
}

impl<'a, T> EntryReader<'a, T>
where
    T: Read + Seek,
{
    pub fn read_at(&mut self, offset: u64) -> Result<Vec<u8>, Error> {
        if offset > self.length {
            return Err(format_err!("Offset exceeds length of entry"));
        }
        self.source.seek(SeekFrom::Start(self.offset + offset))?;
        let clamped_length = self.length - offset;

        let mut data = vec![0; clamped_length as usize];
        self.source.read_exact(&mut data)?;
        Ok(data)
    }
}

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
mod tests {

    use bincode::{deserialize_from, serialize_into, Infinite};
    use failure::Error;
    use itertools::assert_equal;
    use std::collections::HashMap;
    use std::fs::create_dir_all;
    use std::fs::File;
    use std::io::{Cursor, Seek, SeekFrom, Write};
    use std::str;
    use tempdir::TempDir;
    use {align, write, DirectoryEntry, Index, IndexEntry, Reader, DIRECTORY_ENTRY_LEN, DIR_CHUNK,
         INDEX_ENTRY_LEN, INDEX_LEN, MAGIC_INDEX_VALUE};

    fn create_test_files(file_names: &[&str]) -> Result<TempDir, Error> {
        let tmp_dir = TempDir::new("fuchsia_archive_test")?;
        for file_name in file_names {
            let file_path = tmp_dir.path().join(file_name);
            let parent_dir = file_path.parent().unwrap();
            create_dir_all(&parent_dir)?;
            let file_path = tmp_dir.path().join(file_name);
            let mut tmp_file = File::create(&file_path)?;
            writeln!(tmp_file, "{}", file_name)?;
        }
        Ok(tmp_dir)
    }

    fn example_archive() -> Vec<u8> {
        let mut b: Vec<u8> = vec![0; 16384];
        #[cfg_attr(rustfmt, rustfmt_skip)]
        let header = vec![
            /* magic */
            0xc8, 0xbf, 0x0b, 0x48, 0xad, 0xab, 0xc5, 0x11,
            /* length of index entries */
            0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* chunk type */
            0x44, 0x49, 0x52, 0x2d, 0x2d, 0x2d, 0x2d, 0x2d,
            /* offset to chunk */
            0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* length of chunk */
            0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* chunk type */
            0x44, 0x49, 0x52, 0x4e, 0x41, 0x4d, 0x45, 0x53,
            /* offset to chunk */
            0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            /* length of chunk */
            0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
            0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x02, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00,
            0x00, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x61, 0x62, 0x64, 0x69, 0x72, 0x2f, 0x63, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        ];
        b[0..header.len()].copy_from_slice(header.as_slice());
        let name_a = b"a\n";
        let a_loc = 4096;
        b[a_loc..a_loc + name_a.len()].copy_from_slice(name_a);
        let name_b = b"b\n";
        let b_loc = 8192;
        b[b_loc..b_loc + name_b.len()].copy_from_slice(name_b);
        let name_c = b"dir/c\n";
        let c_loc = 12288;
        b[c_loc..c_loc + name_c.len()].copy_from_slice(name_c);
        b
    }

    fn empty_archive() -> Vec<u8> {
        vec![
            0xc8, 0xbf, 0xb, 0x48, 0xad, 0xab, 0xc5, 0x11, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0,
        ]
    }

    #[test]
    fn test_write() {
        let files = ["b", "a", "dir/c"];
        let test_dir = create_test_files(&files).unwrap();
        let mut inputs: HashMap<String, String> = HashMap::new();
        for file_name in files.iter() {
            let path = test_dir
                .path()
                .join(file_name)
                .to_string_lossy()
                .to_string();
            inputs.insert(file_name.to_string(), path);
        }
        let mut target = Cursor::new(Vec::new());
        write(
            &mut target,
            &mut inputs.iter().map(|(a, b)| (a.as_str(), b.as_str())),
        ).unwrap();
        assert!(target.get_ref()[0..8] == MAGIC_INDEX_VALUE);
        let example_archive = example_archive();
        let target_ref = target.get_ref();
        assert_equal(target_ref, &example_archive);
        assert_eq!(*target_ref, example_archive);
    }

    #[test]
    fn test_serialize_index() {
        let mut target = Cursor::new(Vec::new());
        let index = Index {
            magic: MAGIC_INDEX_VALUE,
            length: 2 * INDEX_ENTRY_LEN as u64,
        };
        serialize_into(&mut target, &index, Infinite).unwrap();
        assert_eq!(target.get_ref().len() as u64, INDEX_LEN);
        target.seek(SeekFrom::Start(0)).unwrap();

        let decoded_index: Index = deserialize_from(&mut target, Infinite).unwrap();
        assert_eq!(index, decoded_index);
    }

    #[test]
    fn test_serialize_index_entry() {
        let mut target = Cursor::new(Vec::new());
        let index_entry = IndexEntry {
            chunk_type: DIR_CHUNK as u64,
            offset: 999,
            length: 444,
        };
        serialize_into(&mut target, &index_entry, Infinite).unwrap();
        assert_eq!(target.get_ref().len() as u64, INDEX_ENTRY_LEN);
        target.seek(SeekFrom::Start(0)).unwrap();

        let decoded_index_entry: IndexEntry = deserialize_from(&mut target, Infinite).unwrap();
        assert_eq!(index_entry, decoded_index_entry);
    }

    #[test]
    fn test_serialize_directory_entry() {
        let mut target = Cursor::new(Vec::new());
        let index_entry = DirectoryEntry {
            name_offset: 33,
            name_length: 66,
            reserved: 0,
            data_offset: 99,
            data_length: 1011,
            reserved2: 0,
        };
        serialize_into(&mut target, &index_entry, Infinite).unwrap();
        assert_eq!(target.get_ref().len() as u64, DIRECTORY_ENTRY_LEN);
        target.seek(SeekFrom::Start(0)).unwrap();

        let decoded_index_entry: DirectoryEntry = deserialize_from(&mut target, Infinite).unwrap();
        assert_eq!(index_entry, decoded_index_entry);
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

    fn corrupt_magic(b: &mut Vec<u8>) {
        b[0] = 0;
    }

    fn corrupt_index_length(b: &mut Vec<u8>) {
        let v: u64 = 1;
        let mut cursor = Cursor::new(b);
        cursor.seek(SeekFrom::Start(8)).unwrap();
        serialize_into(&mut cursor, &v, Infinite).unwrap();
    }

    fn corrupt_dir_index_type(b: &mut Vec<u8>) {
        let v: u8 = 255;
        let mut cursor = Cursor::new(b);
        cursor.seek(SeekFrom::Start(INDEX_LEN)).unwrap();
        serialize_into(&mut cursor, &v, Infinite).unwrap();
    }

    #[test]
    fn test_reader() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        let mut reader = Reader::new(&mut example_cursor).unwrap();

        let corrupters = [corrupt_magic, corrupt_index_length, corrupt_dir_index_type];
        let mut index = 0;
        for corrupter in corrupters.iter() {
            let mut example = example_archive();
            corrupter(&mut example);
            let mut example_cursor = Cursor::new(&mut example);
            let mut reader = Reader::new(&mut example_cursor);
            assert!(reader.is_err(), "corrupter index = {}", index);
            index += 1;
        }
    }

    #[test]
    fn test_list_files() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        let mut reader = Reader::new(&mut example_cursor).unwrap();

        let mut files = reader.list().unwrap();
        let want = ["a", "b", "dir/c"];
        files.sort();
        assert_equal(want.iter(), &files);
    }

    #[test]
    fn test_reader_open() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        let mut reader = Reader::new(&mut example_cursor).unwrap();
        assert_eq!(3, reader.directory_entries.len());

        for one_name in ["frobulate", "dir/enhunts"].iter() {
            let entry_reader = reader.open(one_name);
            assert!(
                entry_reader.is_err(),
                "Expected error for archive path \"{}\"",
                one_name
            );
        }

        for one_name in ["a", "b", "dir/c"].iter() {
            let mut entry_reader = reader.open(one_name).unwrap();
            let expected_error = entry_reader.read_at(99);
            assert!(
                expected_error.is_err(),
                "Expected error for offset that exceeds length"
            );
            let content = entry_reader.read_at(0).unwrap();
            let content_str = str::from_utf8(&content).unwrap();
            let expected = format!("{}\n", one_name);
            assert_eq!(content_str, &expected);
        }
    }

    #[test]
    fn test_reader_read_file() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        let mut reader = Reader::new(&mut example_cursor).unwrap();
        for one_name in ["a", "b", "dir/c"].iter() {
            let content = reader.read_file(one_name).unwrap();
            let content_str = str::from_utf8(&content).unwrap();
            let expected = format!("{}\n", one_name);
            assert_eq!(content_str, &expected);
        }
    }

    #[test]
    fn test_reader_get_size() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        let mut reader = Reader::new(&mut example_cursor).unwrap();
        for one_name in ["a", "b", "dir/c"].iter() {
            let returned_size = reader.get_size(one_name).unwrap();
            let expected_size = one_name.len() + 1;
            assert_eq!(returned_size, expected_size as u64);
        }
    }

    #[test]
    fn test_read_empty() {
        let empty = empty_archive();
        let mut empty_cursor = Cursor::new(&empty);
        let mut reader = Reader::new(&mut empty_cursor);
        assert!(reader.is_err(), "Expected error for empty archive");
    }

    #[test]
    fn test_is_far() {}
}
