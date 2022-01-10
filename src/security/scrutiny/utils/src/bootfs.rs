// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    byteorder::{LittleEndian, ReadBytesExt},
    log::trace,
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
    std::convert::{TryFrom, TryInto},
    std::io::{Cursor, Read, Seek, SeekFrom},
    thiserror::Error,
};

const BOOTFS_MAGIC: u32 = 0xa56d3ff9;

#[allow(dead_code)]
#[derive(Serialize, Deserialize)]
struct BootfsHeader {
    magic: u32,
    dir_size: u32,
    reserved_0: u32,
    reserved_1: u32,
}

impl BootfsHeader {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        Ok(Self {
            magic: cursor.read_u32::<LittleEndian>()?,
            dir_size: cursor.read_u32::<LittleEndian>()?,
            reserved_0: cursor.read_u32::<LittleEndian>()?,
            reserved_1: cursor.read_u32::<LittleEndian>()?,
        })
    }
}

#[allow(dead_code)]
#[derive(Serialize, Deserialize)]
struct BootfsDirectoryEntry {
    name_len: u32,
    data_len: u32,
    data_offset: u32,
    name: String,
}

impl BootfsDirectoryEntry {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        let name_len = cursor.read_u32::<LittleEndian>()?;
        let data_len = cursor.read_u32::<LittleEndian>()?;
        let data_offset = cursor.read_u32::<LittleEndian>()?;
        let mut name_buffer: Vec<u8> = Vec::with_capacity(name_len.try_into()?);

        for _i in 0..name_len {
            name_buffer.push(cursor.read_u8()?);
        }
        let name = std::str::from_utf8(&name_buffer)?;

        Ok(Self { name_len, data_len, data_offset, name: name.to_string() })
    }
}

#[derive(Error, Debug)]
pub enum BootfsError {
    #[error("Bootfs magic doesn't match expected")]
    BootfsMagicInvalid,
}

/// Responsible for extracting the zbi from the package and reading the zbi
/// data from it.
#[allow(dead_code)]
pub struct BootfsReader {
    cursor: Cursor<Vec<u8>>,
}

impl BootfsReader {
    pub fn new(buffer: Vec<u8>) -> Self {
        Self { cursor: Cursor::new(buffer) }
    }

    pub fn parse(&mut self) -> Result<HashMap<String, Vec<u8>>> {
        let header = BootfsHeader::parse(&mut self.cursor)?;
        if header.magic != BOOTFS_MAGIC {
            return Err(Error::new(BootfsError::BootfsMagicInvalid {}));
        }
        let mut directory_entries = vec![];

        // Read all the directory entries.
        let end_position = self.cursor.position() + u64::try_from(header.dir_size)? - 1;
        while self.cursor.position() < end_position {
            let start = self.cursor.position();

            directory_entries.push(BootfsDirectoryEntry::parse(&mut self.cursor)?);

            let end = self.cursor.position();
            let diff = end - start;
            // Directory entries are variable in size but must end up 4 byte aligned.
            if diff % 4 != 0 {
                let padding: i64 = (4 - (diff % 4)).try_into().unwrap();
                self.cursor.seek(SeekFrom::Current(padding))?;
            }
        }

        // Parse all the directory entries and retrieve the files.
        let mut files = HashMap::new();
        for directory in directory_entries.iter() {
            trace!(
                "Extracting bootfs file: {}, offset: {}, len: {}",
                directory.name,
                directory.data_offset,
                directory.data_len
            );
            self.cursor.set_position(directory.data_offset.into());
            let mut file_data = vec![0; directory.data_len.try_into()?];
            self.cursor.read_exact(&mut file_data)?;

            let mut dir_name = directory.name.clone();
            if let Some(stripped_path) = dir_name.strip_suffix("\u{0000}") {
                dir_name = stripped_path.to_string();
            }
            files.insert(dir_name, file_data);
        }
        Ok(files)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_bootfs_empty() {
        let bootfs_header =
            BootfsHeader { magic: BOOTFS_MAGIC, dir_size: 0, reserved_0: 0, reserved_1: 0 };
        let bytes = bincode::serialize(&bootfs_header).unwrap();
        let mut reader = BootfsReader::new(bytes);
        let files = reader.parse().unwrap();
        assert_eq!(files.len(), 0);
    }

    #[test]
    fn test_bootfs_with_file() {
        // Get the meta bytes size, we don't serialize the raw structure here
        // since it contains a String which isn't the true underlying packed
        // data type. So we manually dump the data as if it was a packed struct.
        let name_len: u32 = 3;
        let data_len: u32 = 10;
        let mut data_offset: u32 = 0;
        let mut meta_bytes: Vec<u8> = vec![];
        meta_bytes.extend(&name_len.to_le_bytes());
        meta_bytes.extend(&data_len.to_le_bytes());
        meta_bytes.extend(&data_offset.to_le_bytes());
        meta_bytes.push(0x41 as u8);
        meta_bytes.push(0x42 as u8);
        meta_bytes.push(0x43 as u8);

        // Construct the bootfs header using the total size of the file.
        let bootfs_header = BootfsHeader {
            magic: BOOTFS_MAGIC,
            dir_size: u32::try_from(meta_bytes.len()).unwrap(),
            reserved_0: 0,
            reserved_1: 0,
        };
        let mut bytes = bincode::serialize(&bootfs_header).unwrap();

        // Update the bootfs files to calculate its offset from the header + dir.
        data_offset = u32::try_from(bytes.len() + meta_bytes.len()).unwrap();
        meta_bytes.clear();
        meta_bytes.extend(&name_len.to_le_bytes());
        meta_bytes.extend(&data_len.to_le_bytes());
        meta_bytes.extend(&data_offset.to_le_bytes());
        meta_bytes.push(0x41 as u8);
        meta_bytes.push(0x42 as u8);
        meta_bytes.push(0x43 as u8);
        bytes.extend(&meta_bytes);

        let file_data = vec![1; data_len.try_into().unwrap()];
        bytes.extend(&file_data);

        let mut reader = BootfsReader::new(bytes);
        let files = reader.parse().unwrap();
        assert_eq!(files.len(), 1);
        assert_eq!(files["ABC"].len(), 10);
        for i in 0..10 {
            assert_eq!(files["ABC"][i], 1);
        }
    }
}
