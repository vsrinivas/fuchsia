// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_archive::{ChunkType, DIR_CHUNK_TYPE, DIR_NAMES_CHUNK_TYPE, MAGIC_INDEX_VALUE},
    std::{
        fs::{create_dir_all, File},
        io::Write,
        path::PathBuf,
    },
};

const ALL_ZEROES_CHUNK_TYPE: ChunkType = [0u8; 8];

/// Top-level command.
#[derive(argh::FromArgs, Debug)]
struct Opt {
    /// directory to write the invalid FARs.
    #[argh(option)]
    output_dir: PathBuf,
}

fn main() {
    let opt: Opt = argh::from_env();
    create_dir_all(&opt.output_dir).unwrap();

    // Make a file and serialize a sequence of `Serialize` expressions to it.
    macro_rules! make_test_file {
        ( $filename:literal, $( $serializable:expr, )+ ) => {
            let mut file = File::create(opt.output_dir.join($filename)).unwrap();
            $( $serializable.serialize(&mut file); )+
        }
    }

    // The Directory and Directory Names chunks are required, but the chunks themselves can be
    // empty, so the smallest valid FAR is an Index chunk with entries for zero length Directory and
    // Directory Names chunks.
    make_test_file!(
        "invalid-magic-bytes.far",
        0u64,      // invalid magic bytes
        2 * 24u64, // length of index entries in bytes
        IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 0 },
        IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 64, len: 0 },
    );

    make_test_file!(
        "index-entries-length-not-a-multiple-of-24-bytes.far",
        MAGIC_INDEX_VALUE,
        2 * 24 + 1u64, // length of index entries in bytes, should be 48
        IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 0 },
        IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 64, len: 0 },
    );

    make_test_file!(
        "directory-names-index-entry-before-directory-index-entry.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 64, len: 0 },
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 0 },
            ]
        },
    );

    make_test_file!(
        "two-directory-index-entries.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 88, len: 0 },
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 88, len: 0 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 88, len: 0 },
            ]
        },
    );

    make_test_file!(
        "two-directory-names-index-entries.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 88, len: 0 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 88, len: 0 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 88, len: 0 },
            ]
        },
    );

    make_test_file!(
        "duplicate-index-entries-of-unknown-type.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: ALL_ZEROES_CHUNK_TYPE, offset: 112, len: 0 },
                IndexEntry { chunk_type: ALL_ZEROES_CHUNK_TYPE, offset: 112, len: 0 },
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 112, len: 0 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 112, len: 0 },
            ]
        },
    );

    make_test_file!("no-index-entries.far", Index { entries: &[] },);

    make_test_file!(
        "no-directory-index-entry.far",
        Index { entries: &[IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 40, len: 0 },] },
    );

    make_test_file!(
        "no-directory-names-index-entry.far",
        Index { entries: &[IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 40, len: 0 },] },
    );

    make_test_file!(
        "directory-chunk-length-not-a-multiple-of-32-bytes.far",
        Index {
            entries: &[
                IndexEntry {
                    chunk_type: DIR_CHUNK_TYPE,
                    offset: 64,
                    len: 40 /*should be 32*/
                },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 104, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PadTo(104), // padding for incorrect directory chunk length
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "directory-chunk-not-tightly-packed.far",
        Index {
            entries: &[
                IndexEntry {
                    chunk_type: DIR_CHUNK_TYPE,
                    offset: 72, /*should be 64*/
                    len: 32
                },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 104, len: 8 },
            ]
        },
        PadTo(72), // padding for incorrect directory chunk offset
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "directory-names-chunk-not-tightly-packed.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry {
                    chunk_type: DIR_NAMES_CHUNK_TYPE,
                    offset: 104, /*should be 96*/
                    len: 8
                },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PadTo(104), // padding for incorrect directory names chunk offset
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "path-data-offset-too-large.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (9 /*should be 0*/, 1), content: (4096, 0) },
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "path-data-length-too-large.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 9 /*should be 1*/), content: (4096, 0) },
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "directory-entries-not-sorted.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 64 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 128, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        DirectoryEntry { name: (1, 1), content: (4096, 0) },
        PathData("ba"), // b > a
        PadTo(4096),
    );

    make_test_file!(
        "directory-entries-with-same-name.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 64 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 128, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        DirectoryEntry { name: (1, 1), content: (4096, 0) },
        PathData("aa"), // both names are "a"
        PadTo(4096),
    );

    make_test_file!(
        "directory-names-chunk-length-not-a-multiple-of-8-bytes.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry {
                    chunk_type: DIR_NAMES_CHUNK_TYPE,
                    offset: 96,
                    len: 1 /*should be 8*/
                },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "directory-names-chunk-before-directory-chunk.far",
        Index {
            entries: &[
                IndexEntry {
                    chunk_type: DIR_CHUNK_TYPE,
                    offset: 72, /*should be 64*/
                    len: 32
                },
                IndexEntry {
                    chunk_type: DIR_NAMES_CHUNK_TYPE,
                    offset: 64, /*should be 96*/
                    len: 8
                },
            ]
        },
        PathData("a"),
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PadTo(4096),
    );

    make_test_file!(
        "directory-names-chunk-overlaps-directory-chunk.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry {
                    chunk_type: DIR_NAMES_CHUNK_TYPE,
                    offset: 88, /*should be 96*/
                    len: 8
                },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PathData("a"),
        PadTo(4096),
    );

    make_test_file!(
        "zero-length-name.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 0 /*zero length name*/), content: (4096, 0) },
        PathData(""),
        PadTo(4096),
    );

    make_test_file!(
        "name-with-null-character.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PathData("\0"), // null character in name
        PadTo(4096),
    );

    make_test_file!(
        "name-with-leading-slash.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 2), content: (4096, 0) },
        PathData("/a"), // name starts with slash
        PadTo(4096),
    );

    make_test_file!(
        "name-with-trailing-slash.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 2), content: (4096, 0) },
        PathData("a/"), // name ends with slash
        PadTo(4096),
    );

    make_test_file!(
        "name-with-empty-segment.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 4), content: (4096, 0) },
        PathData("a//a"), // name with empty segment
        PadTo(4096),
    );

    make_test_file!(
        "name-with-dot-segment.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 5), content: (4096, 0) },
        PathData("a/./a"), // name with dot segment
        PadTo(4096),
    );

    make_test_file!(
        "name-with-dot-dot-segment.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 6), content: (4096, 0) },
        PathData("a/../a"), // name with dot segment
        PadTo(4096),
    );

    make_test_file!(
        "content-chunk-starts-early.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4095 /*should be 4096*/, 1) },
        PathData("a"),
        PadTo(4095),
        "a",
    );

    make_test_file!(
        "content-chunk-starts-late.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4097 /*should be 4096*/, 1) },
        PathData("a"),
        PadTo(4097),
        "a",
        PadTo(8192),
    );

    make_test_file!(
        "second-content-chunk-starts-early.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 64 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 128, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 1) },
        DirectoryEntry { name: (1, 1), content: (2 * 4096 - 1 /*should be 2*4096*/, 1) },
        PathData("ab"),
        PadTo(4096 * 2),
    );

    make_test_file!(
        "second-content-chunk-starts-late.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 64 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 128, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 1) },
        DirectoryEntry { name: (1, 1), content: (2 * 4096 + 1 /*should be 2*4096*/, 1) },
        PathData("ab"),
        PadTo(4096 * 3),
    );

    make_test_file!(
        "content-chunk-not-zero-padded.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 1) },
        PathData("a"),
        PadTo(4096),
        "a", // Note content chunk not padded to 4192 alignment
    );

    make_test_file!(
        "content-chunk-overlap.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 64 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 128, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 4097) },
        DirectoryEntry { name: (1, 1), content: (2 * 4096 /*should be 3*4096*/, 1) },
        PathData("ab"),
        PadTo(4096 * 3),
    );

    make_test_file!(
        "content-chunk-not-tightly-packed.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096 * 2 /*should be 4096*/, 1) },
        PathData("a"),
        PadTo(4096 * 3),
    );

    make_test_file!(
        "content-chunk-offset-past-end-of-file.far",
        Index {
            entries: &[
                IndexEntry { chunk_type: DIR_CHUNK_TYPE, offset: 64, len: 32 },
                IndexEntry { chunk_type: DIR_NAMES_CHUNK_TYPE, offset: 96, len: 8 },
            ]
        },
        DirectoryEntry { name: (0, 1), content: (4096, 0) },
        PathData("a"),
        // Note no padding
    );
}

trait Serialize {
    fn serialize(&self, f: &mut File);
}

impl Serialize for u64 {
    fn serialize(&self, f: &mut File) {
        f.write_all(&self.to_le_bytes()).unwrap()
    }
}

impl Serialize for u32 {
    fn serialize(&self, f: &mut File) {
        f.write_all(&self.to_le_bytes()).unwrap()
    }
}

impl Serialize for u16 {
    fn serialize(&self, f: &mut File) {
        f.write_all(&self.to_le_bytes()).unwrap()
    }
}

impl Serialize for [u8; 8] {
    fn serialize(&self, f: &mut File) {
        f.write_all(&self[..]).unwrap()
    }
}

impl Serialize for &str {
    fn serialize(&self, f: &mut File) {
        f.write_all(self.as_bytes()).unwrap()
    }
}

// Pad the file with zeroes until it is size self.0, panics if file size already >= self.0
struct PadTo(u64);
impl Serialize for PadTo {
    fn serialize(&self, f: &mut File) {
        let n = f.metadata().unwrap().len();
        assert!(
            self.0 >= n,
            "unnecessary padding requested, asked to pad to {} but file size is already {}",
            self.0,
            n
        );
        let zeroes = vec![0u8; (self.0 - n) as usize];
        f.write_all(&zeroes).unwrap();
    }
}

impl Serialize for Vec<u8> {
    fn serialize(&self, f: &mut File) {
        f.write_all(self).unwrap();
    }
}

struct IndexEntry {
    chunk_type: ChunkType,
    offset: u64,
    len: u64,
}
impl Serialize for IndexEntry {
    fn serialize(&self, f: &mut File) {
        self.chunk_type.serialize(f);
        self.offset.serialize(f);
        self.len.serialize(f);
    }
}

struct Index {
    entries: &'static [IndexEntry],
}
impl Serialize for Index {
    fn serialize(&self, f: &mut File) {
        MAGIC_INDEX_VALUE.serialize(f);
        ((self.entries.len() * 24) as u64).serialize(f);
        for entry in self.entries {
            entry.serialize(f);
        }
    }
}

struct DirectoryEntry {
    // (offset, length)
    name: (u32, u16),
    // (offset, length)
    content: (u64, u64),
}
impl Serialize for DirectoryEntry {
    fn serialize(&self, f: &mut File) {
        self.name.0.serialize(f);
        self.name.1.serialize(f);
        0u16.serialize(f); // padding
        self.content.0.serialize(f);
        self.content.1.serialize(f);
        0u64.serialize(f); // padding
    }
}

// Zero pads to 8 byte alignment
struct PathData(&'static str);
impl Serialize for PathData {
    fn serialize(&self, f: &mut File) {
        self.0.serialize(f);
        let padding = vec![0u8; 8 - (self.0.len() % 8)];
        padding.serialize(f);
    }
}
