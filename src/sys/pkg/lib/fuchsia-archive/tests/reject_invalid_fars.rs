// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    fuchsia_archive::{Error, Reader, DIR_CHUNK_TYPE, DIR_NAMES_CHUNK_TYPE},
    matches::assert_matches,
    std::{fs::File, path::Path},
};

// Creates a test fn named after the first parameter that:
//   1. opens a FAR file named after the first parameter
//   2. calls fuchsia_archive::Reader::new on the file
//   3. asserts that the new() result matches the second parameter
macro_rules! tests {
    ( $( $fn:ident => $err:pat $(if $guard:expr)? , )+ ) => {
        $(
            #[test]
            fn $fn() {
                let mut filename = stringify!($fn).replace("_", "-");
                filename.push_str(".far");
                let file = File::open(Path::new("/pkg/data").join(filename)).unwrap();
                assert_matches!(Reader::new(file), $err $(if $guard)?);
            }
        )+
    };
}

tests! {
    invalid_magic_bytes => Err(Error::InvalidMagic([0, 0, 0, 0, 0, 0, 0, 0])),

    index_entries_length_not_a_multiple_of_24_bytes => Err(Error::InvalidIndexEntriesLen(49)),

    directory_names_index_entry_before_directory_index_entry =>
        Err(Error::IndexEntriesOutOfOrder { prev: DIR_NAMES_CHUNK_TYPE, next: DIR_CHUNK_TYPE }),

    two_directory_index_entries =>
        Err(Error::IndexEntriesOutOfOrder { prev: DIR_CHUNK_TYPE, next: DIR_CHUNK_TYPE }),

    two_directory_names_index_entries =>
        Err(Error::IndexEntriesOutOfOrder {
            prev: DIR_NAMES_CHUNK_TYPE,
            next: DIR_NAMES_CHUNK_TYPE
        }),

    no_directory_index_entry => Err(Error::MissingDirectoryChunkIndexEntry),

    no_directory_names_index_entry => Err(Error::MissingDirectoryNamesChunkIndexEntry),

    directory_chunk_length_not_a_multiple_of_32_bytes => Err(Error::InvalidDirectoryChunkLen(40)),

    directory_chunk_not_tightly_packed =>
        Err(Error::InvalidChunkOffset { chunk_type: DIR_CHUNK_TYPE, expected: 64, actual: 72 }),

    path_data_offset_too_large =>
        Err(Error::PathDataOffsetTooLarge { entry_index: 0, offset: 9, chunk_size: 8 }),

    path_data_length_too_large =>
        Err(Error::PathDataLengthTooLarge { entry_index: 0, offset: 0, length: 9, chunk_size: 8 }),

    directory_entries_not_sorted =>
        Err(Error::DirectoryEntriesOutOfOrder { entry_index: 1, name, previous_name})
            if name == "a" && previous_name == "b",

    directory_entries_with_same_name =>
        Err(Error::DirectoryEntriesOutOfOrder { entry_index: 1, name, previous_name})
            if name == "a" && previous_name == "a",

    directory_names_chunk_length_not_a_multiple_of_8_bytes =>
        Err(Error::InvalidDirectoryNamesChunkLen(1)),

    directory_names_chunk_not_tightly_packed =>
        Err(Error::InvalidChunkOffset {
            chunk_type: DIR_NAMES_CHUNK_TYPE,
            expected: 96,
            actual: 104
        }),

    directory_names_chunk_before_directory_chunk =>
        Err(Error::InvalidChunkOffset { chunk_type: DIR_CHUNK_TYPE, expected: 64, actual: 72 }),

    directory_names_chunk_overlaps_directory_chunk =>
        Err(Error::InvalidChunkOffset {
            chunk_type: DIR_NAMES_CHUNK_TYPE,
            expected: 96,
            actual: 88
        }),

    zero_length_name =>  Err(Error::ZeroLengthName),

    name_with_null_character =>  Err(Error::NameContainsNull(name)) if name == "\0",

    name_with_leading_slash =>  Err(Error::NameStartsWithSlash(name)) if name == "/a",

    name_with_trailing_slash =>  Err(Error::NameEndsWithSlash(name)) if name == "a/",

    name_with_empty_segment =>  Err(Error::NameContainsEmptySegment(name)) if name == "a//a",

    name_with_dot_segment =>  Err(Error::NameContainsDotSegment(name)) if name == "a/./a",

    name_with_dot_dot_segment =>  Err(Error::NameContainsDotDotSegment(name)) if name == "a/../a",

    content_chunk_starts_early =>
        Err(Error::InvalidContentChunkOffset{name, expected: 4096, actual: 4095}) if name == "a",

    content_chunk_starts_late =>
        Err(Error::InvalidContentChunkOffset{name, expected: 4096, actual: 4097}) if name == "a",

    second_content_chunk_starts_early =>
        Err(Error::InvalidContentChunkOffset{name, expected: 8192, actual: 8191}) if name == "b",

    second_content_chunk_starts_late =>
        Err(Error::InvalidContentChunkOffset{name, expected: 8192, actual: 8193}) if name == "b",

    content_chunk_not_zero_padded =>
        Err(Error::ContentChunkBeyondArchive{name, lower_bound: 8192, archive_size: 4097})
        if name == "a",

    content_chunk_overlap =>
        Err(Error::InvalidContentChunkOffset{name, expected: 12288, actual: 8192}) if name == "b",

    content_chunk_not_tightly_packed =>
        Err(Error::InvalidContentChunkOffset{name, expected: 4096, actual: 8192}) if name == "a",

    content_chunk_offset_past_end_of_file =>
        Err(Error::ContentChunkBeyondArchive{
            name,
            lower_bound: 4096,
            archive_size: 104})
        if name == "a",
}
