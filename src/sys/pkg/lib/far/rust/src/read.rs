// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        align, name::validate_name, DirectoryEntry, Error, Index, IndexEntry, CONTENT_ALIGNMENT,
        DIRECTORY_ENTRY_LEN, DIR_CHUNK_TYPE, DIR_NAMES_CHUNK_TYPE, INDEX_ENTRY_LEN, INDEX_LEN,
        MAGIC_INDEX_VALUE,
    },
    bincode::deserialize_from,
    std::{
        collections::BTreeMap,
        io::{Read, Seek, SeekFrom},
        str,
    },
};

/// A struct to open and read FAR-formatted archive.
#[derive(Debug)]
pub struct Reader<T>
where
    T: Read + Seek,
{
    source: T,
    directory_entries: BTreeMap<String, DirectoryEntry>,
}

impl<T> Reader<T>
where
    T: Read + Seek,
{
    /// Create a new Reader for the provided source.
    /// Requires UTF-8 names, which is stricter than the FAR spec.
    pub fn new(mut source: T) -> Result<Reader<T>, Error> {
        let index = Self::read_index(&mut source)?;

        let (dir_index, dir_name_index, end_of_last_non_content_chunk) =
            Reader::<T>::read_index_entries(&mut source, index.length / INDEX_ENTRY_LEN, &index)?;

        let dir_index = dir_index.ok_or(Error::MissingDirectoryChunkIndexEntry)?;
        let dir_name_index = dir_name_index.ok_or(Error::MissingDirectoryNamesChunkIndexEntry)?;

        let stream_len = source.seek(SeekFrom::End(0)).map_err(Error::Seek)?;

        // DIRNAMES chunk must include padding to next 8 byte boundary
        if dir_name_index.length % 8 != 0 {
            return Err(Error::InvalidDirectoryNamesChunkLen(dir_name_index.length));
        }
        source.seek(SeekFrom::Start(dir_name_index.offset)).map_err(Error::Seek)?;
        let mut path_data = vec![0; dir_name_index.length as usize];
        source.read_exact(&mut path_data).map_err(Error::Read)?;

        source.seek(SeekFrom::Start(dir_index.offset)).map_err(Error::Seek)?;
        if dir_index.length % DIRECTORY_ENTRY_LEN != 0 {
            return Err(Error::InvalidDirectoryChunkLen(dir_index.length));
        }
        let dir_entry_count = dir_index.length / DIRECTORY_ENTRY_LEN;
        let mut directory_entries = BTreeMap::new();
        let mut previous_name: Option<&str> = None;
        let mut previous_entry: Option<DirectoryEntry> = None;
        for i in 0..dir_entry_count {
            let entry: DirectoryEntry =
                deserialize_from(&mut source).map_err(Error::DeserializeDirectoryEntry)?;

            let name = Self::name_for_entry(&entry, i, &path_data, previous_name)?;

            let () = Self::validate_content_chunk(
                &entry,
                previous_entry.as_ref(),
                name,
                stream_len,
                end_of_last_non_content_chunk,
            )?;

            directory_entries.insert(name.to_string(), entry);
            previous_name = Some(name);
            previous_entry = Some(entry);
        }

        Ok(Reader { source, directory_entries })
    }

    // Obtain name for current directory entry, making sure it is a valid name and lexicographically
    // than the previous name.
    fn name_for_entry<'a>(
        entry: &DirectoryEntry,
        entry_index: u64,
        path_data: &'a [u8],
        previous_name: Option<&str>,
    ) -> Result<&'a str, Error> {
        let offset = entry.name_offset as usize;
        if offset >= path_data.len() {
            return Err(Error::PathDataOffsetTooLarge {
                entry_index,
                offset,
                chunk_size: path_data.len(),
            });
        }

        let end = offset + entry.name_length as usize;
        if end > path_data.len() {
            return Err(Error::PathDataLengthTooLarge {
                entry_index,
                offset,
                length: entry.name_length,
                chunk_size: path_data.len(),
            });
        }

        let name = validate_name(&path_data[offset..end])?;
        // FAR spec does not require that names be valid utf8, but this library does
        let name = str::from_utf8(name).map_err(Error::PathDataInvalidUtf8)?;

        // Directory entries must be strictly increasing by name
        if let Some(previous_name) = previous_name {
            if previous_name >= name {
                return Err(Error::DirectoryEntriesOutOfOrder {
                    entry_index,
                    previous_name: previous_name.to_owned(),
                    name: name.to_owned(),
                });
            }
        }
        Ok(name)
    }

    fn validate_content_chunk(
        entry: &DirectoryEntry,
        previous_entry: Option<&DirectoryEntry>,
        name: &str,
        stream_len: u64,
        end_of_last_non_content_chunk: u64,
    ) -> Result<(), Error> {
        // Chunks must be non-overlapping and tightly packed
        let expected_offset = if let Some(previous_entry) = previous_entry {
            align(previous_entry.data_offset + previous_entry.data_length, CONTENT_ALIGNMENT)
        } else {
            align(end_of_last_non_content_chunk, CONTENT_ALIGNMENT)
        };
        if entry.data_offset != expected_offset {
            return Err(Error::InvalidContentChunkOffset {
                name: name.to_owned(),
                expected: expected_offset,
                actual: entry.data_offset,
            });
        }

        // Chunks must be contained in the archive
        let stream_len_lower_bound =
            align(entry.data_offset + entry.data_length, CONTENT_ALIGNMENT);
        if stream_len_lower_bound > stream_len {
            return Err(Error::ContentChunkBeyondArchive {
                name: name.to_owned(),
                lower_bound: stream_len_lower_bound,
                archive_size: stream_len,
            });
        }
        Ok(())
    }

    /// Return a list of the items in the archive
    pub fn list(&self) -> impl Iterator<Item = &str> {
        self.directory_entries.keys().map(String::as_str)
    }

    fn read_index(source: &mut T) -> Result<Index, Error> {
        let decoded_index: Index =
            deserialize_from(&mut *source).map_err(Error::DeserializeIndex)?;
        if decoded_index.magic != MAGIC_INDEX_VALUE {
            Err(Error::InvalidMagic(decoded_index.magic))
        } else if decoded_index.length % INDEX_ENTRY_LEN != 0 {
            Err(Error::InvalidIndexEntriesLen(decoded_index.length))
        } else {
            Ok(decoded_index)
        }
    }

    // Returns (directory_index, directory_names_index, end_of_last_chunk).
    // Assumes `source` cursor is at the beginning of the index entries.
    fn read_index_entries(
        source: &mut T,
        count: u64,
        index: &Index,
    ) -> Result<(Option<IndexEntry>, Option<IndexEntry>, u64), Error> {
        let mut dir_index: Option<IndexEntry> = None;
        let mut dir_name_index: Option<IndexEntry> = None;
        let mut previous_entry: Option<IndexEntry> = None;
        for _ in 0..count {
            let entry: IndexEntry =
                deserialize_from(&mut *source).map_err(Error::DeserializeIndexEntry)?;

            let expected_offset = if let Some(previous_entry) = previous_entry {
                if previous_entry.chunk_type >= entry.chunk_type {
                    return Err(Error::IndexEntriesOutOfOrder {
                        prev: previous_entry.chunk_type,
                        next: entry.chunk_type,
                    });
                }
                previous_entry.offset + previous_entry.length
            } else {
                INDEX_LEN + index.length
            };
            if entry.offset != expected_offset {
                return Err(Error::InvalidChunkOffset {
                    chunk_type: entry.chunk_type,
                    expected: expected_offset,
                    actual: entry.offset,
                });
            }

            match entry.chunk_type {
                DIR_NAMES_CHUNK_TYPE => {
                    dir_name_index = Some(entry);
                }
                DIR_CHUNK_TYPE => {
                    dir_index = Some(entry);
                }
                // FAR spec does not forbid unknown chunk types
                _ => {}
            }
            previous_entry = Some(entry);
        }
        let end_of_last_chunk = if let Some(previous_entry) = previous_entry {
            previous_entry.offset + previous_entry.length
        } else {
            INDEX_LEN
        };
        Ok((dir_index, dir_name_index, end_of_last_chunk))
    }

    fn find_directory_entry(&self, archive_path: &str) -> Result<&DirectoryEntry, Error> {
        self.directory_entries
            .get(archive_path)
            .ok_or_else(|| Error::PathNotPresent(archive_path.to_string()))
    }

    /// Create an EntryReader for an entry with the specified name.
    pub fn open(&mut self, archive_path: &str) -> Result<EntryReader<'_, T>, Error> {
        let directory_entry = self.find_directory_entry(archive_path)?;

        Ok(EntryReader {
            offset: directory_entry.data_offset,
            length: directory_entry.data_length,
            source: &mut self.source,
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
pub struct EntryReader<'a, T>
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
            return Err(Error::ReadPastEnd);
        }
        self.source.seek(SeekFrom::Start(self.offset + offset)).map_err(Error::Seek)?;
        let clamped_length = self.length - offset;

        let mut data = vec![0; clamped_length as usize];
        self.source.read_exact(&mut data).map_err(Error::Read)?;
        Ok(data)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{tests::example_archive, INDEX_LEN},
        bincode::serialize_into,
        itertools::assert_equal,
        std::{
            io::{Cursor, Seek, SeekFrom},
            str,
        },
    };

    fn empty_archive() -> Vec<u8> {
        vec![0xc8, 0xbf, 0xb, 0x48, 0xad, 0xab, 0xc5, 0x11, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0]
    }

    fn corrupt_magic(b: &mut Vec<u8>) {
        b[0] = 0;
    }

    fn corrupt_index_length(b: &mut Vec<u8>) {
        let v: u64 = 1;
        let mut cursor = Cursor::new(b);
        cursor.seek(SeekFrom::Start(8)).unwrap();
        serialize_into(&mut cursor, &v).unwrap();
    }

    fn corrupt_dir_index_type(b: &mut Vec<u8>) {
        let v: u8 = 255;
        let mut cursor = Cursor::new(b);
        cursor.seek(SeekFrom::Start(INDEX_LEN)).unwrap();
        serialize_into(&mut cursor, &v).unwrap();
    }

    #[test]
    fn test_reader() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        assert!(Reader::new(&mut example_cursor).is_ok());

        let corrupters = [corrupt_magic, corrupt_index_length, corrupt_dir_index_type];
        let mut index = 0;
        for corrupter in corrupters.iter() {
            let mut example = example_archive();
            corrupter(&mut example);
            let mut example_cursor = Cursor::new(&mut example);
            let reader = Reader::new(&mut example_cursor);
            assert!(reader.is_err(), "corrupter index = {}", index);
            index += 1;
        }
    }

    #[test]
    fn test_list_files() {
        let example = example_archive();
        let mut example_cursor = Cursor::new(&example);
        let reader = Reader::new(&mut example_cursor).unwrap();

        let files = reader.list().collect::<Vec<_>>();
        let want = ["a", "b", "dir/c"];
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
            assert!(entry_reader.is_err(), "Expected error for archive path \"{}\"", one_name);
        }

        for one_name in ["a", "b", "dir/c"].iter() {
            let mut entry_reader = reader.open(one_name).unwrap();
            let expected_error = entry_reader.read_at(99);
            assert!(expected_error.is_err(), "Expected error for offset that exceeds length");
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
        let reader = Reader::new(&mut empty_cursor);
        assert!(reader.is_err(), "Expected error for empty archive");
    }
}
