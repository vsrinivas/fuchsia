// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        name::validate_name, next_multiple_of, DirectoryEntry, Error, Index, IndexEntry,
        CONTENT_ALIGNMENT, DIRECTORY_ENTRY_LEN, DIR_CHUNK_TYPE, DIR_NAMES_CHUNK_TYPE,
        INDEX_ENTRY_LEN, INDEX_LEN, MAGIC_INDEX_VALUE,
    },
    std::{
        collections::BTreeMap,
        convert::TryInto as _,
        io::{copy, Read, Write},
    },
    zerocopy::AsBytes as _,
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
    path_content_map: BTreeMap<impl AsRef<[u8]>, (u64, Box<dyn Read + '_>)>,
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
        validate_name(destination_name)?;
        directory_entries.push(DirectoryEntry {
            name_offset: u32::try_from(path_data.len()).map_err(|_| Error::TooMuchPathData)?.into(),
            name_length: destination_name
                .len()
                .try_into()
                .map_err(|_| Error::NameTooLong(destination_name.len()))?,
            reserved: 0.into(),
            data_offset: 0.into(),
            data_length: (*size).into(),
            reserved2: 0.into(),
        });
        path_data.extend_from_slice(destination_name.as_bytes());
    }

    let index = Index { magic: MAGIC_INDEX_VALUE, length: (2 * INDEX_ENTRY_LEN).into() };

    let dir_index = IndexEntry {
        chunk_type: DIR_CHUNK_TYPE,
        offset: (INDEX_LEN + INDEX_ENTRY_LEN * 2).into(),
        length: (directory_entries.len() as u64 * DIRECTORY_ENTRY_LEN).into(),
    };

    let name_index = IndexEntry {
        chunk_type: DIR_NAMES_CHUNK_TYPE,
        offset: (dir_index.offset.get() + dir_index.length.get()).into(),
        length: next_multiple_of(path_data.len() as u64, 8).into(),
    };

    target.write_all(index.as_bytes()).map_err(Error::SerializeIndex)?;

    target.write_all(dir_index.as_bytes()).map_err(Error::SerializeDirectoryChunkIndexEntry)?;

    target
        .write_all(name_index.as_bytes())
        .map_err(Error::SerializeDirectoryNamesChunkIndexEntry)?;

    let mut content_offset =
        next_multiple_of(name_index.offset.get() + name_index.length.get(), CONTENT_ALIGNMENT);

    for entry in &mut directory_entries {
        entry.data_offset = content_offset.into();
        content_offset =
            next_multiple_of(content_offset + entry.data_length.get(), CONTENT_ALIGNMENT);
        target.write_all(entry.as_bytes()).map_err(Error::SerializeDirectoryEntry)?;
    }

    target.write_all(&path_data).map_err(Error::Write)?;

    write_zeros(&mut target, name_index.length.get() as usize - path_data.len())?;

    let pos = name_index.offset.get() + name_index.length.get();
    let padding_count = next_multiple_of(pos, CONTENT_ALIGNMENT) - pos;
    write_zeros(&mut target, padding_count as usize)?;

    for (entry_index, (archive_path, (_, mut contents))) in path_content_map.into_iter().enumerate()
    {
        let bytes_read = copy(&mut contents, &mut target).map_err(Error::Copy)?;
        if bytes_read != directory_entries[entry_index].data_length.get() {
            return Err(Error::ContentChunkSizeMismatch {
                expected: directory_entries[entry_index].data_length.get(),
                actual: bytes_read,
                path: archive_path.as_ref().into(),
            });
        }
        let pos = directory_entries[entry_index].data_offset.get()
            + directory_entries[entry_index].data_length.get();
        let padding_count = next_multiple_of(pos, CONTENT_ALIGNMENT) - pos;
        write_zeros(&mut target, padding_count as usize)?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::tests::example_archive,
        assert_matches::assert_matches,
        itertools::assert_equal,
        std::{
            collections::BTreeMap,
            io::{Cursor, Read},
        },
    };

    #[test]
    fn creates_example_archive() {
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

    #[test]
    fn validates_name() {
        let path_content_map =
            BTreeMap::from_iter([(".", (0, Box::new("".as_bytes()) as Box<dyn Read>))]);
        let mut target = Cursor::new(Vec::new());
        assert_matches!(
            write(&mut target, path_content_map),
            Err(Error::NameContainsDotSegment(_))
        );
    }

    #[test]
    fn validates_name_length() {
        let name = String::from_utf8(vec![b'a'; 2usize.pow(16)]).unwrap();
        let mut path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = BTreeMap::new();
        path_content_map.insert(&name, (0, Box::new("".as_bytes())));
        let mut target = Cursor::new(Vec::new());
        assert_matches!(
            write(&mut target, path_content_map),
            Err(Error::NameTooLong(len)) if len == 2usize.pow(16)
        );
    }
}
