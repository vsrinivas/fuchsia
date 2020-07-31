// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        align, DirectoryEntry, Error, Index, IndexEntry, CONTENT_ALIGNMENT, DIRECTORY_ENTRY_LEN,
        DIR_CHUNK, DIR_NAMES_CHUNK, INDEX_ENTRY_LEN, INDEX_LEN, MAGIC_INDEX_VALUE,
    },
    bincode::serialize_into,
    std::{
        collections::BTreeMap,
        io::{copy, Read, Write},
    },
};

fn write_zeros(mut target: impl Write, count: usize) -> Result<(), Error> {
    let b = vec![0u8; count];
    target.write_all(&b).map_err(Error::Write)?;
    Ok(())
}

/// Write a FAR-formatted archive to the target.
///
/// # Arguments
///
/// * `target` - receives the serialized bytes of the archive
/// * `path_content_map` - map from archive relative path to (size, contents)
pub fn write(
    mut target: impl Write,
    path_content_map: BTreeMap<impl AsRef<str>, (u64, Box<dyn Read + '_>)>,
) -> Result<(), Error> {
    // `write` could be written to take the content chunks as one of:
    // 1. Box<dyn Read>: requires an allocation per chunk
    // 2. &mut dyn Read: requires that the caller retain ownership of the chunks, which could result
    //                   in the caller building a mirror data structure just to own the chunks
    // 3. impl Read: requires that all the chunks have the same type
    let mut path_data: Vec<u8> = vec![];
    let mut directory_entries = vec![];
    for (destination_name, (size, _)) in &path_content_map {
        let destination_name = destination_name.as_ref();
        if destination_name.len() > u16::max_value() as usize {
            return Err(Error::NameTooLong(destination_name.len()));
        }
        directory_entries.push(DirectoryEntry {
            name_offset: path_data.len() as u32,
            name_length: destination_name.len() as u16,
            reserved: 0,
            data_offset: 0,
            data_length: *size,
            reserved2: 0,
        });
        path_data.extend_from_slice(destination_name.as_bytes());
    }

    let index = Index { magic: MAGIC_INDEX_VALUE, length: 2 * INDEX_ENTRY_LEN as u64 };

    let dir_index = IndexEntry {
        chunk_type: DIR_CHUNK as u64,
        offset: INDEX_LEN + INDEX_ENTRY_LEN * 2,
        length: directory_entries.len() as u64 * DIRECTORY_ENTRY_LEN,
    };

    let name_index = IndexEntry {
        chunk_type: DIR_NAMES_CHUNK as u64,
        offset: dir_index.offset + dir_index.length,
        length: align(path_data.len() as u64, 8),
    };

    serialize_into(&mut target, &index).map_err(Error::SerializeIndex)?;

    serialize_into(&mut target, &dir_index).map_err(Error::SerializeDirectoryChunkIndexEntry)?;

    serialize_into(&mut target, &name_index)
        .map_err(Error::SerializeDirectoryNamesChunkIndexEntry)?;

    let mut content_offset = align(name_index.offset + name_index.length, CONTENT_ALIGNMENT);

    for entry in &mut directory_entries {
        entry.data_offset = content_offset;
        content_offset = align(content_offset + entry.data_length, CONTENT_ALIGNMENT);
        serialize_into(&mut target, &entry).map_err(Error::SerializeDirectoryEntry)?;
    }

    target.write_all(&path_data).map_err(Error::Write)?;

    write_zeros(&mut target, name_index.length as usize - path_data.len())?;

    let pos = name_index.offset + name_index.length;
    let padding_count = align(pos, CONTENT_ALIGNMENT) - pos;
    write_zeros(&mut target, padding_count as usize)?;

    for (entry_index, (archive_path, (_, mut contents))) in path_content_map.into_iter().enumerate()
    {
        let bytes_read = copy(&mut contents, &mut target).map_err(Error::Copy)?;
        if bytes_read != directory_entries[entry_index].data_length {
            return Err(Error::ContentChunkSizeMismatch {
                expected: directory_entries[entry_index].data_length,
                actual: bytes_read,
                path: archive_path.as_ref().to_string(),
            });
        }
        let pos =
            directory_entries[entry_index].data_offset + directory_entries[entry_index].data_length;
        let padding_count = align(pos, CONTENT_ALIGNMENT) - pos;
        write_zeros(&mut target, padding_count as usize)?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::tests::example_archive,
        itertools::assert_equal,
        std::{
            collections::BTreeMap,
            io::{Cursor, Read},
        },
    };

    #[test]
    fn test_write() {
        let a_contents = "a\n".as_bytes();
        let b_contents = "b\n".as_bytes();
        let dirc_contents = "dir/c\n".as_bytes();
        let mut path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = BTreeMap::new();
        path_content_map.insert("a", (a_contents.len() as u64, Box::new(a_contents)));
        path_content_map.insert("b", (b_contents.len() as u64, Box::new(b_contents)));
        path_content_map.insert("dir/c", (dirc_contents.len() as u64, Box::new(dirc_contents)));
        let mut target = Cursor::new(Vec::new());
        write(&mut target, path_content_map).unwrap();
        assert!(target.get_ref()[0..8] == MAGIC_INDEX_VALUE);
        let example_archive = example_archive();
        let target_ref = target.get_ref();
        assert_equal(target_ref, &example_archive);
        assert_eq!(*target_ref, example_archive);
    }
}
