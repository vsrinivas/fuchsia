// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        DirectoryEntry, Error, Index, IndexEntry, DIRECTORY_ENTRY_LEN, DIR_CHUNK_TYPE,
        DIR_NAMES_CHUNK_TYPE, INDEX_ENTRY_LEN, INDEX_LEN, MAGIC_INDEX_VALUE,
    },
    std::{
        convert::TryInto as _,
        io::{Read, Seek, SeekFrom},
    },
    zerocopy::AsBytes as _,
};

/// A struct to open and read FAR-formatted archive.
#[derive(Debug)]
pub struct Reader<T>
where
    T: Read + Seek,
{
    source: T,
    directory_entries: Box<[DirectoryEntry]>,
    path_data: Box<[u8]>,
}

impl<T> Reader<T>
where
    T: Read + Seek,
{
    /// Create a new Reader for the provided source.
    pub fn new(mut source: T) -> Result<Self, Error> {
        let index = Self::read_index_header(&mut source)?;
        let (dir_index, dir_name_index, end_of_last_non_content_chunk) =
            Self::read_index_entries(&mut source, &index)?;
        let stream_len = source.seek(SeekFrom::End(0)).map_err(Error::Seek)?;

        // Read directory entries
        if dir_index.length.get() % DIRECTORY_ENTRY_LEN != 0 {
            return Err(Error::InvalidDirectoryChunkLen(dir_index.length.get()));
        }
        let mut directory_entries =
            vec![
                DirectoryEntry::default();
                (dir_index.length.get() / DIRECTORY_ENTRY_LEN)
                    .try_into()
                    .map_err(|_| { Error::InvalidDirectoryChunkLen(dir_index.length.get()) })?
            ];
        source.seek(SeekFrom::Start(dir_index.offset.get())).map_err(Error::Seek)?;
        source.read_exact(directory_entries.as_bytes_mut()).map_err(Error::Read)?;
        let directory_entries = directory_entries.into_boxed_slice();

        // Read path data
        if dir_name_index.length.get() % 8 != 0 || dir_name_index.length.get() > stream_len {
            return Err(Error::InvalidDirectoryNamesChunkLen(dir_name_index.length.get()));
        }
        let path_data_length = dir_name_index
            .length
            .get()
            .try_into()
            .map_err(|_| Error::InvalidDirectoryNamesChunkLen(dir_name_index.length.get()))?;
        let mut path_data = vec![0; path_data_length];
        source.seek(SeekFrom::Start(dir_name_index.offset.get())).map_err(Error::Seek)?;
        source.read_exact(path_data.as_mut_slice()).map_err(Error::Read)?;
        let path_data = path_data.into_boxed_slice();

        let () = crate::validate_directory_entries_and_paths(
            &directory_entries,
            &path_data,
            stream_len,
            end_of_last_non_content_chunk,
        )?;

        Ok(Self { source, directory_entries, path_data })
    }

    // Assumes `source` cursor is at the beginning of the file.
    fn read_index_header(source: &mut T) -> Result<Index, Error> {
        let mut index = Index::default();
        source.read_exact(index.as_bytes_mut()).map_err(Error::Read)?;
        if index.magic != MAGIC_INDEX_VALUE {
            Err(Error::InvalidMagic(index.magic))
        } else if index.length.get() % INDEX_ENTRY_LEN != 0
            || INDEX_LEN.checked_add(index.length.get()).is_none()
        {
            Err(Error::InvalidIndexEntriesLen(index.length.get()))
        } else {
            Ok(index)
        }
    }

    // Returns (directory_index, directory_names_index, end_of_last_chunk).
    // Assumes `source` cursor is at the beginning of the index entries.
    fn read_index_entries(
        source: &mut T,
        index: &Index,
    ) -> Result<(IndexEntry, IndexEntry, u64), Error> {
        let mut dir_index: Option<IndexEntry> = None;
        let mut dir_name_index: Option<IndexEntry> = None;
        let mut previous_entry: Option<IndexEntry> = None;
        for _ in 0..index.length.get() / INDEX_ENTRY_LEN {
            let mut entry = IndexEntry::default();
            source.read_exact(entry.as_bytes_mut()).map_err(Error::Read)?;

            let expected_offset = if let Some(previous_entry) = previous_entry {
                if previous_entry.chunk_type >= entry.chunk_type {
                    return Err(Error::IndexEntriesOutOfOrder {
                        prev: previous_entry.chunk_type,
                        next: entry.chunk_type,
                    });
                }
                previous_entry.offset.get() + previous_entry.length.get()
            } else {
                INDEX_LEN + index.length.get()
            };
            if entry.offset.get() != expected_offset {
                return Err(Error::InvalidChunkOffset {
                    chunk_type: entry.chunk_type,
                    expected: expected_offset,
                    actual: entry.offset.get(),
                });
            }
            if entry.offset.get().checked_add(entry.length.get()).is_none() {
                return Err(Error::InvalidChunkLength {
                    chunk_type: entry.chunk_type,
                    offset: entry.offset.get(),
                    length: entry.length.get(),
                });
            }

            match entry.chunk_type {
                DIR_CHUNK_TYPE => {
                    dir_index = Some(entry);
                }
                DIR_NAMES_CHUNK_TYPE => {
                    dir_name_index = Some(entry);
                }
                // FAR spec does not forbid unknown chunk types
                _ => {}
            }
            previous_entry = Some(entry);
        }
        let end_of_last_chunk = if let Some(previous_entry) = previous_entry {
            previous_entry.offset.get() + previous_entry.length.get()
        } else {
            INDEX_LEN
        };
        Ok((
            dir_index.ok_or(Error::MissingDirectoryChunkIndexEntry)?,
            dir_name_index.ok_or(Error::MissingDirectoryNamesChunkIndexEntry)?,
            end_of_last_chunk,
        ))
    }

    /// Return a list of the items in the archive
    pub fn list(&self) -> impl ExactSizeIterator<Item = crate::Entry<'_>> {
        crate::list(&self.directory_entries, &self.path_data)
    }

    /// Read the entire contents of the entry with the specified path.
    /// O(log(# directory entries))
    pub fn read_file(&mut self, path: &[u8]) -> Result<Vec<u8>, Error> {
        let entry = crate::find_directory_entry(&self.directory_entries, &self.path_data, path)?;
        let mut data = vec![
            0;
            usize::try_from(entry.data_length.get()).map_err(|_| {
                Error::ContentChunkDoesNotFitInMemory {
                    name: path.into(),
                    chunk_size: entry.data_length.get(),
                }
            })?
        ];
        let _: u64 =
            self.source.seek(SeekFrom::Start(entry.data_offset.get())).map_err(Error::Seek)?;
        let () = self.source.read_exact(&mut data).map_err(Error::Read)?;
        Ok(data)
    }

    /// Get the size in bytes of the entry with the specified path.
    /// O(log(# directory entries))
    pub fn get_size(&mut self, path: &[u8]) -> Result<u64, Error> {
        Ok(crate::find_directory_entry(&self.directory_entries, &self.path_data, path)?
            .data_length
            .get())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::tests::example_archive, assert_matches::assert_matches, std::io::Cursor,
    };

    #[test]
    fn list() {
        let example = example_archive();
        let reader = Reader::new(Cursor::new(&example)).unwrap();
        itertools::assert_equal(
            reader.list(),
            [
                crate::Entry { path: b"a", offset: 4096, length: 2 },
                crate::Entry { path: b"b", offset: 8192, length: 2 },
                crate::Entry { path: b"dir/c", offset: 12288, length: 6 },
            ],
        );
    }

    #[test]
    fn read_file() {
        let example = example_archive();
        let mut reader = Reader::new(Cursor::new(&example)).unwrap();
        for one_name in ["a", "b", "dir/c"].iter().map(|s| s.as_bytes()) {
            let content = reader.read_file(one_name).unwrap();
            let content_str = std::str::from_utf8(&content).unwrap();
            let expected = format!("{}\n", std::str::from_utf8(one_name).unwrap());
            assert_eq!(content_str, &expected);
        }
    }

    #[test]
    fn get_size() {
        let example = example_archive();
        let mut reader = Reader::new(Cursor::new(&example)).unwrap();
        for one_name in ["a", "b", "dir/c"].iter().map(|s| s.as_bytes()) {
            let returned_size = reader.get_size(one_name).unwrap();
            let expected_size = one_name.len() + 1;
            assert_eq!(returned_size, u64::try_from(expected_size).unwrap());
        }
    }

    #[test]
    fn accessors_error_on_missing_path() {
        let example = example_archive();
        let mut reader = Reader::new(Cursor::new(&example)).unwrap();
        assert_matches!(
            reader.read_file(b"missing-path"),
            Err(Error::PathNotPresent(path)) if path == b"missing-path"
        );
        assert_matches!(
            reader.get_size(b"missing-path"),
            Err(Error::PathNotPresent(path)) if path == b"missing-path"
        );
    }
}
