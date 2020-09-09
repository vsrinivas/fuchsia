// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod bootfs;

use {
    bootfs::{
        zbi_bootfs_dirent_t, zbi_bootfs_header_t, ZBI_BOOTFS_MAGIC, ZBI_BOOTFS_MAX_NAME_LEN,
        ZBI_BOOTFS_PAGE_SIZE,
    },
    byteorder::{ByteOrder, LittleEndian},
    fuchsia_zircon as zx,
    std::{ffi::CStr, mem::size_of, str::Utf8Error},
    thiserror::Error,
    zerocopy::{ByteSlice, LayoutVerified},
};

const ZBI_BOOTFS_DIRENT_SIZE: usize = size_of::<zbi_bootfs_dirent_t>();
const ZBI_BOOTFS_HEADER_SIZE: usize = size_of::<zbi_bootfs_header_t>();

// Each directory entry has a variable size of [16,268] bytes that
// must be a multiple of 4 bytes.
fn zbi_bootfs_dirent_size(name_len: u32) -> u32 {
    (ZBI_BOOTFS_DIRENT_SIZE as u32 + name_len + 3) & !3u32
}

fn zbi_bootfs_page_align(size: u32) -> u32 {
    size.wrapping_add(ZBI_BOOTFS_PAGE_SIZE - 1) & !(ZBI_BOOTFS_PAGE_SIZE - 1)
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum BootfsParserError {
    #[error("Invalid magic for bootfs payload")]
    BadMagic,

    #[error("Directory entry {} exceeds expected dirsize of {}", entry_index, dirsize)]
    DirEntryTooBig { entry_index: u32, dirsize: u32 },

    #[error("Failed to read payload: {}", status)]
    FailedToReadPayload { status: zx::Status },

    #[error("Failed to parse bootfs header")]
    FailedToParseHeader,

    #[error("Failed to parse directory entry")]
    FailedToParseDirEntry,

    #[error("Failed to read name as UTF-8: {}", cause)]
    InvalidNameFormat {
        #[source]
        cause: Utf8Error,
    },

    #[error("Failed to find null terminated string for name: {}", cause)]
    InvalidNameString {
        #[source]
        cause: std::ffi::FromBytesWithNulError,
    },

    #[error(
        "name_len must be between 1 and {}, found {} for directory entry {}",
        max_name_len,
        name_len,
        entry_index
    )]
    InvalidNameLength { name_len: u32, max_name_len: u32, entry_index: u32 },
}

#[derive(Debug)]
struct ZbiBootfsDirent<B: ByteSlice> {
    header: LayoutVerified<B, zbi_bootfs_dirent_t>,
    name_bytes: B,
}
impl<B: ByteSlice> ZbiBootfsDirent<B> {
    pub fn parse(bytes: B) -> Result<ZbiBootfsDirent<B>, BootfsParserError> {
        let (header, name_bytes) =
            LayoutVerified::<B, zbi_bootfs_dirent_t>::new_unaligned_from_prefix(bytes)
                .ok_or(BootfsParserError::FailedToParseDirEntry)?;

        Ok(ZbiBootfsDirent { header, name_bytes })
    }

    pub fn data_len(&self) -> u32 {
        return self.header.data_len.get();
    }

    pub fn data_off(&self) -> u32 {
        return self.header.data_off.get();
    }

    pub fn name(&self) -> Result<&str, BootfsParserError> {
        // Name is stored as a array reference to a block of characters.
        // Characters must be UTF-8 encoded.
        // Valid names are terminated with NUL.
        // We should fail if either of the above conditions are not met.
        match CStr::from_bytes_with_nul(&self.name_bytes[..self.header.name_len.get() as usize]) {
            Ok(bytes) => {
                bytes.to_str().map_err(|cause| BootfsParserError::InvalidNameFormat { cause })
            }
            Err(cause) => Err(BootfsParserError::InvalidNameString { cause }),
        }
    }
}

/// Parser for bootfs-formatted structures.
#[derive(Debug)]
pub struct BootfsParser {
    // Expose fields for BootfsParserIterator access.
    pub(self) dirsize: u32,
    pub(self) vmo: zx::Vmo,
}
impl BootfsParser {
    /// Creates a BootfsParser from an existing VMO.
    ///
    /// If `vmo` contains invalid header data, BootfsParserError is returned.
    pub fn create_from_vmo(vmo: zx::Vmo) -> Result<BootfsParser, BootfsParserError> {
        let mut header_bytes = [0; ZBI_BOOTFS_HEADER_SIZE];
        vmo.read(&mut header_bytes, 0)
            .map_err(|status| BootfsParserError::FailedToReadPayload { status })?;

        let header = LayoutVerified::<_, zbi_bootfs_header_t>::new_unaligned(&header_bytes[..])
            .ok_or(BootfsParserError::FailedToParseHeader)?;
        if header.magic.get() == ZBI_BOOTFS_MAGIC {
            Ok(Self { vmo, dirsize: header.dirsize.get() })
        } else {
            Err(BootfsParserError::BadMagic)
        }
    }

    pub fn iter(&self) -> impl Iterator<Item = Result<BootfsEntry, BootfsParserError>> + '_ {
        BootfsParserIterator::new(&self)
    }
}

#[derive(Debug)]
pub struct BootfsEntry {
    pub name: String,
    pub payload: Vec<u8>,
}

#[derive(Debug)]
struct BootfsParserIterator<'parser> {
    available_dirsize: u32,
    dir_offset: u32,
    entry_index: u32,
    errored: bool,
    parser: &'parser BootfsParser,
}
impl<'parser> BootfsParserIterator<'parser> {
    pub fn new(parser: &'parser BootfsParser) -> Self {
        Self {
            available_dirsize: parser.dirsize,
            dir_offset: ZBI_BOOTFS_HEADER_SIZE as u32,
            entry_index: 0,
            errored: false,
            parser,
        }
    }
}

impl<'parser> Iterator for BootfsParserIterator<'parser> {
    type Item = Result<BootfsEntry, BootfsParserError>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.available_dirsize <= ZBI_BOOTFS_DIRENT_SIZE as u32 || self.errored {
            return None;
        }

        // Read the name_len field only.
        let mut name_len_buf = [0; size_of::<u32>()];
        if let Err(status) = self.parser.vmo.read(&mut name_len_buf, self.dir_offset.into()) {
            self.errored = true;
            return Some(Err(BootfsParserError::FailedToReadPayload { status }));
        }

        let name_len = LittleEndian::read_u32(&name_len_buf);
        if name_len < 1 || name_len > ZBI_BOOTFS_MAX_NAME_LEN {
            self.errored = true;
            return Some(Err(BootfsParserError::InvalidNameLength {
                entry_index: self.entry_index,
                max_name_len: ZBI_BOOTFS_MAX_NAME_LEN,
                name_len,
            }));
        }

        let dirent_size = zbi_bootfs_dirent_size(name_len);
        if dirent_size > self.available_dirsize {
            self.errored = true;
            return Some(Err(BootfsParserError::DirEntryTooBig {
                dirsize: self.parser.dirsize,
                entry_index: self.entry_index,
            }));
        }

        // Now that we know how long the name is, read the whole entry.
        let mut dirent_buffer = vec![0; dirent_size as usize];
        if let Err(status) = self.parser.vmo.read(&mut dirent_buffer, self.dir_offset.into()) {
            self.errored = true;
            return Some(Err(BootfsParserError::FailedToReadPayload { status }));
        }

        match ZbiBootfsDirent::parse(&dirent_buffer[..]) {
            Ok(dirent) => {
                // We have a directory entry now, so retrieve the payload.
                let mut payload = vec![0; dirent.data_len() as usize];
                if let Err(status) = self
                    .parser
                    .vmo
                    .read(&mut payload, zbi_bootfs_page_align(dirent.data_off()).into())
                {
                    self.errored = true;
                    return Some(Err(BootfsParserError::FailedToReadPayload { status }));
                }

                self.dir_offset += dirent_buffer.len() as u32;
                self.available_dirsize -= dirent_size;
                self.entry_index += 1;

                Some(dirent.name().map(|name| BootfsEntry { name: name.to_owned(), payload }))
            }
            Err(err) => {
                self.errored = true;
                Some(Err(err))
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        lazy_static::lazy_static,
        std::{collections::HashMap, fs::File, io::prelude::*},
    };

    static GOLDEN_DIR: &str = "/pkg/data/golden/";
    static BASIC_BOOTFS_UNCOMPRESSED_FILE: &str = "/pkg/data/basic.bootfs.uncompresssed";

    fn read_file_into_hashmap(dir: &str, filename: &str, map: &mut HashMap<String, Vec<u8>>) {
        let mut file_buffer = Vec::new();
        let path = format!("{}{}", dir, filename);

        File::open(&path)
            .expect(&format!("Failed to open file {}", &path))
            .read_to_end(&mut file_buffer)
            .expect(&format!("Failed to read file {}", &path));
        map.insert(filename.to_string(), file_buffer);
    }

    lazy_static! {
        static ref GOLDEN_FILES: HashMap<String, Vec<u8>> = {
            let mut m = HashMap::new();
            read_file_into_hashmap(GOLDEN_DIR, "dir/empty", &mut m);
            read_file_into_hashmap(GOLDEN_DIR, "dir/lorem.txt", &mut m);
            read_file_into_hashmap(GOLDEN_DIR, "empty", &mut m);
            read_file_into_hashmap(GOLDEN_DIR, "random.dat", &mut m);
            read_file_into_hashmap(GOLDEN_DIR, "simple.txt", &mut m);
            m
        };
    }

    fn read_file_to_vmo(path: &str) -> Result<zx::Vmo, Error> {
        let mut file_buffer = Vec::new();
        File::open(path)?.read_to_end(&mut file_buffer)?;

        let vmo = zx::Vmo::create(file_buffer.len() as u64)?;
        vmo.write(&file_buffer, 0)?;
        Ok(vmo)
    }

    #[test]
    fn dirent_from_raw_fails_on_bad_cstring() {
        const NAME_LEN: u8 = 3;
        let mut dirent_buf = [0; ZBI_BOOTFS_DIRENT_SIZE + NAME_LEN as usize];

        dirent_buf[0] = NAME_LEN;
        dirent_buf[ZBI_BOOTFS_DIRENT_SIZE] = 'o' as u8;
        dirent_buf[ZBI_BOOTFS_DIRENT_SIZE + 1] = 'k' as u8;
        // This should be NUL...but it's not for this test.
        dirent_buf[ZBI_BOOTFS_DIRENT_SIZE + 2] = 'a' as u8;

        let dirent = ZbiBootfsDirent::parse(&dirent_buf[..])
            .expect("Failed to create ZbiBootfsDirent from raw buffer");
        match dirent.name().unwrap_err() {
            BootfsParserError::InvalidNameString { cause: _cause } => (),
            _ => panic!("ZbiBootfsDirent.name did not fail with correct error"),
        }
    }

    #[test]
    fn dirent_from_raw_fails_on_non_utf8_string() {
        const NAME_LEN: u8 = 3;
        let mut dirent_buf = [0; ZBI_BOOTFS_DIRENT_SIZE + NAME_LEN as usize];

        // This is an invalid UTF-8 sequence.
        dirent_buf[0] = NAME_LEN;
        dirent_buf[ZBI_BOOTFS_DIRENT_SIZE] = 0xC3;
        dirent_buf[ZBI_BOOTFS_DIRENT_SIZE + 1] = 0x28;
        dirent_buf[ZBI_BOOTFS_DIRENT_SIZE + 2] = '\0' as u8;

        // Assert that it actually IS an invalid UTF-8 string.
        let char_sequence = &dirent_buf[ZBI_BOOTFS_DIRENT_SIZE..dirent_buf.len()];
        assert_eq!(true, String::from_utf8(char_sequence.to_vec()).is_err());

        let dirent = ZbiBootfsDirent::parse(&dirent_buf[..])
            .expect("Failed to create ZbiBootfsDirent from raw buffer");
        match dirent.name().unwrap_err() {
            BootfsParserError::InvalidNameFormat { cause: _cause } => (),
            _ => panic!("ZbiBootfsDirent.name did not fail with correct error"),
        }
    }

    #[test]
    fn create_bootfs_parser() {
        let vmo = read_file_to_vmo(BASIC_BOOTFS_UNCOMPRESSED_FILE).unwrap();
        assert_eq!(true, BootfsParser::create_from_vmo(vmo).is_ok());
    }

    #[test]
    fn process_basic_bootfs() {
        let vmo = read_file_to_vmo(BASIC_BOOTFS_UNCOMPRESSED_FILE).unwrap();
        let parser = BootfsParser::create_from_vmo(vmo).expect("Failed to read bootfs file");

        let mut files = Box::new(HashMap::new());

        parser.iter().for_each(|result| {
            let result = result.expect("Failed to process bootfs payload");
            let BootfsEntry { name, payload } = result;
            files.insert(name, payload);
        });

        assert_eq!(*GOLDEN_FILES, *files);
    }

    #[test]
    fn process_bootfs_with_invalid_header() {
        let vmo = read_file_to_vmo(BASIC_BOOTFS_UNCOMPRESSED_FILE).unwrap();
        let new_header = [0; ZBI_BOOTFS_HEADER_SIZE];

        // Wipe the header of a known good bootfs payload.
        vmo.write(&new_header, 0).expect("Failed to wipe bootfs header");

        match BootfsParser::create_from_vmo(vmo).unwrap_err() {
            BootfsParserError::BadMagic => (),
            _ => panic!("BootfsParser::create_from_vmo did not fail with correct error"),
        }
    }

    #[test]
    fn process_bootfs_with_invalid_direntry() {
        let vmo = read_file_to_vmo(BASIC_BOOTFS_UNCOMPRESSED_FILE).unwrap();
        let new_header = [0; ZBI_BOOTFS_DIRENT_SIZE];

        // Wipe the first direntry of a known good bootfs payload.
        // The first direntry starts immediately after the header.
        vmo.write(&new_header, ZBI_BOOTFS_HEADER_SIZE as u64).expect("Failed to wipe direntry");

        let parser = BootfsParser::create_from_vmo(vmo).expect("Failed to create BootfsParser");
        parser.iter().for_each(|result| match result.unwrap_err() {
            BootfsParserError::InvalidNameLength { entry_index, max_name_len, name_len } => {
                assert_eq!(0, entry_index);
                assert_eq!(ZBI_BOOTFS_MAX_NAME_LEN, max_name_len);
                assert_eq!(0, name_len);
            }
            _ => panic!("parser did not fail with correct error"),
        });
    }

    #[test]
    fn process_bootfs_undersized_dirsize() {
        let vmo = read_file_to_vmo(BASIC_BOOTFS_UNCOMPRESSED_FILE).unwrap();
        let new_header = [(ZBI_BOOTFS_DIRENT_SIZE + 1) as u8, 0, 0, 0];

        // Change dirsize to ZBI_BOOTFS_DIRENT_SIZE+1.
        // It is the second u32 value in the zbi_bootfs_header_t struct.
        vmo.write(&new_header, size_of::<u32>() as u64).expect("Failed to change dirsize");

        let parser = BootfsParser::create_from_vmo(vmo).expect("Failed to create BootfsParser");
        parser.iter().for_each(|result| match result.unwrap_err() {
            BootfsParserError::DirEntryTooBig { entry_index, dirsize } => {
                assert_eq!(0, entry_index);
                assert_eq!(ZBI_BOOTFS_DIRENT_SIZE + 1, dirsize as usize);
            }
            _ => panic!("parser did not fail with correct error"),
        });
    }
}
