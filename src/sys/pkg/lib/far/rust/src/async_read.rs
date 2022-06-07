// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        read::{name_for_entry, validate_content_chunk},
        DirectoryEntry, Entry, Error, Index, IndexEntry, DIRECTORY_ENTRY_LEN, DIR_CHUNK_TYPE,
        DIR_NAMES_CHUNK_TYPE, INDEX_ENTRY_LEN, INDEX_LEN, MAGIC_INDEX_VALUE,
    },
    bincode::deserialize,
    io_util::file::{AsyncGetSize, AsyncGetSizeExt, AsyncReadAt, AsyncReadAtExt},
    std::{collections::BTreeMap, convert::TryInto as _, str},
};

/// A struct to open and read FAR-formatted archive asynchronously.
#[derive(Debug)]
pub struct AsyncReader<T>
where
    T: AsyncReadAt + AsyncGetSize + Unpin,
{
    source: T,
    directory_entries: BTreeMap<String, DirectoryEntry>,
}

impl<T> AsyncReader<T>
where
    T: AsyncReadAt + AsyncGetSize + Unpin,
{
    /// Create a new AsyncReader for the provided source.
    /// Requires UTF-8 names, which is stricter than the FAR spec.
    pub async fn new(mut source: T) -> Result<AsyncReader<T>, Error> {
        let index = Self::read_index(&mut source).await?;

        let (dir_index, dir_name_index, end_of_last_non_content_chunk) =
            AsyncReader::<T>::read_index_entries(
                &mut source,
                index.length / INDEX_ENTRY_LEN,
                &index,
            )
            .await?;

        let dir_index = dir_index.ok_or(Error::MissingDirectoryChunkIndexEntry)?;
        let dir_name_index = dir_name_index.ok_or(Error::MissingDirectoryNamesChunkIndexEntry)?;

        let stream_len = source.get_size().await.map_err(Error::GetSize)?;

        // DIRNAMES chunk must include padding to next 8 byte boundary
        if dir_name_index.length % 8 != 0 || dir_name_index.length > stream_len {
            return Err(Error::InvalidDirectoryNamesChunkLen(dir_name_index.length));
        }
        let path_data_length = match dir_name_index.length.try_into() {
            Ok(length) => length,
            Err(_) => return Err(Error::InvalidDirectoryNamesChunkLen(dir_name_index.length)),
        };
        let mut path_data = vec![0; path_data_length];
        source.read_at_exact(dir_name_index.offset, &mut path_data).await.map_err(Error::Read)?;

        if dir_index.length % DIRECTORY_ENTRY_LEN != 0 {
            return Err(Error::InvalidDirectoryChunkLen(dir_index.length));
        }
        let dir_entry_count = dir_index.length / DIRECTORY_ENTRY_LEN;
        let mut directory_entries = BTreeMap::new();
        let mut previous_name: Option<&str> = None;
        let mut previous_entry: Option<DirectoryEntry> = None;
        for i in 0..dir_entry_count {
            let mut entry_data = [0; DIRECTORY_ENTRY_LEN as usize];
            source
                .read_at_exact(dir_index.offset + i * DIRECTORY_ENTRY_LEN, &mut entry_data)
                .await
                .map_err(Error::Read)?;
            let entry: DirectoryEntry =
                deserialize(&mut entry_data).map_err(Error::DeserializeDirectoryEntry)?;

            let name = name_for_entry(&entry, i, &path_data, previous_name)?;

            let () = validate_content_chunk(
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

        Ok(Self { source, directory_entries })
    }

    /// Return a list of the items in the archive
    pub fn list(&self) -> impl Iterator<Item = Entry<'_>> {
        (&self.directory_entries).into_iter().map(|(k, v)| Entry {
            path: k,
            offset: v.data_offset,
            length: v.data_length,
        })
    }

    async fn read_index(source: &mut T) -> Result<Index, Error> {
        let mut index = [0; INDEX_LEN as usize];
        source.read_at_exact(0, &mut index).await.map_err(Error::Read)?;
        let decoded_index: Index = deserialize(&index).map_err(Error::DeserializeIndex)?;
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
    async fn read_index_entries(
        source: &mut T,
        count: u64,
        index: &Index,
    ) -> Result<(Option<IndexEntry>, Option<IndexEntry>, u64), Error> {
        let mut dir_index: Option<IndexEntry> = None;
        let mut dir_name_index: Option<IndexEntry> = None;
        let mut previous_entry: Option<IndexEntry> = None;
        for i in 0..count {
            let mut entry_data = [0; INDEX_ENTRY_LEN as usize];
            let entry_offset = INDEX_LEN + INDEX_ENTRY_LEN * i;
            source.read_at_exact(entry_offset, &mut entry_data).await.map_err(Error::Read)?;
            let entry: IndexEntry =
                deserialize(&entry_data).map_err(Error::DeserializeIndexEntry)?;

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
    pub fn open(&mut self, archive_path: &str) -> Result<AsyncEntryReader<'_, T>, Error> {
        let directory_entry = self.find_directory_entry(archive_path)?;

        Ok(AsyncEntryReader {
            offset: directory_entry.data_offset,
            length: directory_entry.data_length,
            source: &mut self.source,
        })
    }

    /// Read the entire contents of an entry with the specified name.
    pub async fn read_file(&mut self, archive_path: &str) -> Result<Vec<u8>, Error> {
        let mut reader = self.open(archive_path)?;
        reader.read_at(0).await
    }

    /// Get the size in bytes of an entry with the specified name.
    pub fn get_size(&mut self, archive_path: &str) -> Result<u64, Error> {
        let directory_entry = self.find_directory_entry(archive_path)?;
        Ok(directory_entry.data_length)
    }
}

/// A structure that allows reading from the offset of an item in a
/// FAR archive.
pub struct AsyncEntryReader<'a, T>
where
    T: AsyncReadAt + AsyncGetSize + Unpin,
{
    offset: u64,
    length: u64,
    source: &'a mut T,
}

impl<'a, T> AsyncEntryReader<'a, T>
where
    T: AsyncReadAt + AsyncGetSize + Unpin,
{
    pub async fn read_at(&mut self, offset: u64) -> Result<Vec<u8>, Error> {
        if offset > self.length {
            return Err(Error::ReadPastEnd);
        }
        let clamped_length = self.length - offset;
        let mut data = vec![0; clamped_length as usize];
        self.source.read_at_exact(self.offset + offset, &mut data).await.map_err(Error::Read)?;
        Ok(data)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{tests::example_archive, INDEX_LEN},
        bincode::serialize_into,
        fuchsia_async as fasync,
        futures::io::Cursor,
        io_util::file::Adapter,
        itertools::assert_equal,
        std::{
            io::{Seek, SeekFrom},
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
        let mut cursor = std::io::Cursor::new(b);
        cursor.seek(SeekFrom::Start(8)).unwrap();
        serialize_into(&mut cursor, &v).unwrap();
    }

    fn corrupt_dir_index_type(b: &mut Vec<u8>) {
        let v: u8 = 255;
        let mut cursor = std::io::Cursor::new(b);
        cursor.seek(SeekFrom::Start(INDEX_LEN)).unwrap();
        serialize_into(&mut cursor, &v).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reader() {
        let example = example_archive();
        let example_cursor = Cursor::new(&example);
        assert!(AsyncReader::new(Adapter::new(example_cursor)).await.is_ok());

        let corrupters = [corrupt_magic, corrupt_index_length, corrupt_dir_index_type];
        let mut index = 0;
        for corrupter in corrupters.iter() {
            let mut example = example_archive();
            corrupter(&mut example);
            let example_cursor = Cursor::new(&mut example);
            let reader = AsyncReader::new(Adapter::new(example_cursor)).await;
            assert!(reader.is_err(), "corrupter index = {}", index);
            index += 1;
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_list_files() {
        let example = example_archive();
        let example_cursor = Cursor::new(&example);
        let reader = AsyncReader::new(Adapter::new(example_cursor)).await.unwrap();

        let files = reader.list().collect::<Vec<_>>();
        let want = [
            Entry { path: "a", offset: 4096, length: 2 },
            Entry { path: "b", offset: 8192, length: 2 },
            Entry { path: "dir/c", offset: 12288, length: 6 },
        ];
        assert_equal(want.iter(), &files);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reader_open() {
        let example = example_archive();
        let example_cursor = Cursor::new(&example);
        let mut reader = AsyncReader::new(Adapter::new(example_cursor)).await.unwrap();
        assert_eq!(3, reader.directory_entries.len());

        for one_name in ["frobulate", "dir/enhunts"].iter() {
            let entry_reader = reader.open(one_name);
            assert!(entry_reader.is_err(), "Expected error for archive path \"{}\"", one_name);
        }

        for one_name in ["a", "b", "dir/c"].iter() {
            let mut entry_reader = reader.open(one_name).unwrap();
            let expected_error = entry_reader.read_at(99).await;
            assert!(expected_error.is_err(), "Expected error for offset that exceeds length");
            let content = entry_reader.read_at(0).await.unwrap();
            let content_str = str::from_utf8(&content).unwrap();
            let expected = format!("{}\n", one_name);
            assert_eq!(content_str, &expected);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reader_read_file() {
        let example = example_archive();
        let example_cursor = Cursor::new(&example);
        let mut reader = AsyncReader::new(Adapter::new(example_cursor)).await.unwrap();
        for one_name in ["a", "b", "dir/c"].iter() {
            let content = reader.read_file(one_name).await.unwrap();
            let content_str = str::from_utf8(&content).unwrap();
            let expected = format!("{}\n", one_name);
            assert_eq!(content_str, &expected);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_reader_get_size() {
        let example = example_archive();
        let example_cursor = Cursor::new(&example);
        let mut reader = AsyncReader::new(Adapter::new(example_cursor)).await.unwrap();
        for one_name in ["a", "b", "dir/c"].iter() {
            let returned_size = reader.get_size(one_name).unwrap();
            let expected_size = one_name.len() + 1;
            assert_eq!(returned_size, expected_size as u64);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_empty() {
        let empty = empty_archive();
        let empty_cursor = Cursor::new(&empty);
        let reader = AsyncReader::new(Adapter::new(empty_cursor)).await;
        assert!(reader.is_err(), "Expected error for empty archive");
    }
}
