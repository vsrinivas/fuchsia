// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::zstd,
    anyhow::{Error, Result},
    byteorder::{LittleEndian, ReadBytesExt},
    log::info,
    scrutiny::model::model::{ZbiSection, ZbiType},
    serde::{Deserialize, Serialize},
    std::convert::{TryFrom, TryInto},
    std::io::{Cursor, Read, Seek, SeekFrom},
    thiserror::Error,
};

/// ZBIs must start with a container type that contains all of the sections in
/// the ZBI. It is largely there to verify the binary blob is actually a ZBI and
/// to inform the reader how far to read into the blob.
const ZBI_TYPE_CONTAINER: u32 = 0x544f4f42;

/// LSW of sha256("bootitem")
const ZBI_ITEM_MAGIC: u32 = 0xb5781729;

/// Set in the flags if the section is compressed. We assume all compression
/// is ZSTD. If this flag is set ZbiHeader.extra will be the uncompressed
/// size of the image.
const ZBI_FLAG_STORAGE_COMPRESSED: u32 = 0x00000001;

/// Rust clone of zircon/boot/image.h
#[allow(dead_code)]
#[derive(Serialize, Deserialize, Clone)]
struct ZbiHeader {
    zbi_type: u32,
    length: u32,
    extra: u32,
    flags: u32,
    reserved_0: u32,
    reserved_1: u32,
    magic: u32,
    crc32: u32,
}

impl ZbiHeader {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        Ok(Self {
            zbi_type: cursor.read_u32::<LittleEndian>()?,
            length: cursor.read_u32::<LittleEndian>()?,
            extra: cursor.read_u32::<LittleEndian>()?,
            flags: cursor.read_u32::<LittleEndian>()?,
            reserved_0: cursor.read_u32::<LittleEndian>()?,
            reserved_1: cursor.read_u32::<LittleEndian>()?,
            magic: cursor.read_u32::<LittleEndian>()?,
            crc32: cursor.read_u32::<LittleEndian>()?,
        })
    }
}

#[derive(Error, Debug)]
pub enum ZbiError {
    #[error("Zbi container header type does not match expected value")]
    InvalidContainerHeader,
    #[error("Zbi container header magic value doesn't match expected value {0}")]
    InvalidContainerMagic(u32),
    #[error("Zbi item header magic value doesn't match expected value")]
    InvalidItemMagic,
}

/// Responsible for extracting the zbi from the package and reading the zbi
/// data from it.
pub struct ZbiReader {
    cursor: Cursor<Vec<u8>>,
}

impl ZbiReader {
    pub fn new(zbi_buffer: Vec<u8>) -> Self {
        Self { cursor: Cursor::new(zbi_buffer) }
    }

    pub fn parse(&mut self) -> Result<Vec<ZbiSection>> {
        // Parse the header and validate it is a ZBI.
        let container_header = ZbiHeader::parse(&mut self.cursor)?;
        if container_header.zbi_type != ZBI_TYPE_CONTAINER {
            return Err(Error::new(ZbiError::InvalidContainerHeader {}));
        }
        if container_header.magic != ZBI_ITEM_MAGIC {
            return Err(Error::new(ZbiError::InvalidContainerMagic(container_header.magic)));
        }
        let container_end = self.cursor.position() + (container_header.length as u64);

        let mut zbi_sections = vec![];

        if container_end == self.cursor.position() {
            return Ok(zbi_sections);
        }

        // Iterate until we cannot parse section headers anymore or reach
        // the end.
        while let Ok(section_header) = ZbiHeader::parse(&mut self.cursor) {
            if section_header.magic != ZBI_ITEM_MAGIC {
                return Err(Error::new(ZbiError::InvalidItemMagic {}));
            }

            let section_type = ZbiReader::section_type(section_header.zbi_type);
            if section_type == ZbiType::Unknown {
                info!("Unknown zbi section: {}", section_header.zbi_type);
            }
            let data_len = usize::try_from(section_header.length)?;
            let mut section_data = vec![0; data_len];
            self.cursor.read_exact(&mut section_data)?;

            // Decompress the block.
            if (section_header.flags & ZBI_FLAG_STORAGE_COMPRESSED) == ZBI_FLAG_STORAGE_COMPRESSED {
                let decompressed_data = zstd::decompress(&section_data, section_header.extra)?;
                zbi_sections.push(ZbiSection { section_type, buffer: decompressed_data });
            } else {
                zbi_sections.push(ZbiSection { section_type, buffer: section_data });
            }

            // All items are 8 byte aligned, skip if the end of the block isn't.
            let position: u64 = self.cursor.position();
            if position % 8 != 0 {
                let padding: u64 = 8 - (position % 8);
                self.cursor.seek(SeekFrom::Current(padding.try_into().unwrap()))?;
            }

            // Exit if we have arrived at the end of the container length.
            if self.cursor.position() >= container_end {
                break;
            }
        }
        Ok(zbi_sections)
    }

    fn section_type(zbi_type: u32) -> ZbiType {
        match zbi_type {
            zbi_type if zbi_type == ZbiType::Discard as u32 => ZbiType::Discard,
            zbi_type if zbi_type == ZbiType::StorageRamdisk as u32 => ZbiType::StorageRamdisk,
            zbi_type if zbi_type == ZbiType::StorageBootfs as u32 => ZbiType::StorageBootfs,
            zbi_type if zbi_type == ZbiType::StorageBootfsFactory as u32 => {
                ZbiType::StorageBootfsFactory
            }
            zbi_type if zbi_type == ZbiType::Cmdline as u32 => ZbiType::Cmdline,
            zbi_type if zbi_type == ZbiType::Crashlog as u32 => ZbiType::Crashlog,
            zbi_type if zbi_type == ZbiType::Nvram as u32 => ZbiType::Nvram,
            zbi_type if zbi_type == ZbiType::NvramDeprecated as u32 => ZbiType::NvramDeprecated,
            zbi_type if zbi_type == ZbiType::PlatformId as u32 => ZbiType::PlatformId,
            zbi_type if zbi_type == ZbiType::DriverBoardInfo as u32 => ZbiType::DriverBoardInfo,
            zbi_type if zbi_type == ZbiType::CpuConfig as u32 => ZbiType::CpuConfig,
            zbi_type if zbi_type == ZbiType::CpuTopology as u32 => ZbiType::CpuTopology,
            zbi_type if zbi_type == ZbiType::MemoryConfig as u32 => ZbiType::MemoryConfig,
            zbi_type if zbi_type == ZbiType::KernelDriver as u32 => ZbiType::KernelDriver,
            zbi_type if zbi_type == ZbiType::AcpiRsdp as u32 => ZbiType::AcpiRsdp,
            zbi_type if zbi_type == ZbiType::Smbios as u32 => ZbiType::Smbios,
            zbi_type if zbi_type == ZbiType::EfiMemoryMap as u32 => ZbiType::EfiMemoryMap,
            zbi_type if zbi_type == ZbiType::EfiSystemTable as u32 => ZbiType::EfiSystemTable,
            zbi_type if zbi_type == ZbiType::E820MemoryTable as u32 => ZbiType::E820MemoryTable,
            zbi_type if zbi_type == ZbiType::FrameBuffer as u32 => ZbiType::FrameBuffer,
            zbi_type if zbi_type == ZbiType::ImageArgs as u32 => ZbiType::ImageArgs,
            zbi_type if zbi_type == ZbiType::BootVersion as u32 => ZbiType::BootVersion,
            zbi_type if zbi_type == ZbiType::DriverMacAddress as u32 => ZbiType::DriverMacAddress,
            zbi_type if zbi_type == ZbiType::DriverPartitionMap as u32 => {
                ZbiType::DriverPartitionMap
            }
            zbi_type if zbi_type == ZbiType::DriverBoardPrivate as u32 => {
                ZbiType::DriverBoardPrivate
            }
            zbi_type if zbi_type == ZbiType::RebootReason as u32 => ZbiType::RebootReason,
            zbi_type if zbi_type == ZbiType::SerialNumber as u32 => ZbiType::SerialNumber,
            zbi_type if zbi_type == ZbiType::BootloaderFile as u32 => ZbiType::BootloaderFile,
            zbi_type if zbi_type == ZbiType::KernelArm64 as u32 => ZbiType::KernelArm64,
            zbi_type if zbi_type == ZbiType::KernelX64 as u32 => ZbiType::KernelX64,
            _ => ZbiType::Unknown,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::convert::TryInto};

    #[test]
    fn test_zbi_empty_container() {
        let container_header = ZbiHeader {
            zbi_type: ZBI_TYPE_CONTAINER,
            length: 0,
            extra: 0,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let zbi_bytes = bincode::serialize(&container_header).unwrap();
        let mut reader = ZbiReader::new(zbi_bytes);
        let sections = reader.parse().unwrap();
        assert_eq!(sections.len(), 0);
    }

    #[test]
    fn test_zbi_sections() {
        let mut container_header = ZbiHeader {
            zbi_type: ZBI_TYPE_CONTAINER,
            length: 0,
            extra: 0,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let section_header = ZbiHeader {
            zbi_type: ZbiType::Discard as u32,
            length: 10,
            extra: 10,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let section_data: Vec<u8> = vec![0; 10];

        let mut section_bytes: Vec<u8> = bincode::serialize(&section_header).unwrap();
        section_bytes.extend(&section_data);
        container_header.length = u32::try_from(section_bytes.len()).unwrap();
        let mut zbi_bytes: Vec<u8> = bincode::serialize(&container_header).unwrap();
        zbi_bytes.extend(&section_bytes);

        let mut reader = ZbiReader::new(zbi_bytes);
        let sections = reader.parse().unwrap();
        assert_eq!(sections.len(), 1);
        assert_eq!(sections[0].buffer.len(), 10);
    }

    #[test]
    fn test_zbi_compressed_sections() {
        let mut container_header = ZbiHeader {
            zbi_type: ZBI_TYPE_CONTAINER,
            length: 0,
            extra: 0,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let uncompressed_len: u32 = 4096;
        let uncompressed_data: Vec<u8> = vec![0; uncompressed_len.try_into().unwrap()];
        let section_data = zstd::compress(&uncompressed_data, uncompressed_len, 3).unwrap();

        let section_header = ZbiHeader {
            zbi_type: ZbiType::Discard as u32,
            length: u32::try_from(section_data.len()).unwrap(),
            extra: 4096,
            flags: ZBI_FLAG_STORAGE_COMPRESSED,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };

        let mut section_bytes: Vec<u8> = bincode::serialize(&section_header).unwrap();
        section_bytes.extend(&section_data);
        container_header.length = u32::try_from(section_bytes.len()).unwrap();
        let mut zbi_bytes: Vec<u8> = bincode::serialize(&container_header).unwrap();
        zbi_bytes.extend(&section_bytes);

        let mut reader = ZbiReader::new(zbi_bytes);
        let sections = reader.parse().unwrap();
        assert_eq!(sections.len(), 1);
        assert_eq!(sections[0].buffer.len(), uncompressed_len as usize);
    }

    #[test]
    fn test_zbi_sections_unaligned() {
        let mut container_header = ZbiHeader {
            zbi_type: ZBI_TYPE_CONTAINER,
            length: 0,
            extra: 0,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let section_header = ZbiHeader {
            zbi_type: ZbiType::Discard as u32,
            length: 7,
            extra: 7,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let section_data: Vec<u8> = vec![0; 7];

        let section_header_two = ZbiHeader {
            zbi_type: ZbiType::Discard as u32,
            length: 10,
            extra: 10,
            flags: 0,
            reserved_0: 0,
            reserved_1: 0,
            magic: ZBI_ITEM_MAGIC,
            crc32: 0,
        };
        let section_data_two: Vec<u8> = vec![0; 10];

        let mut section_bytes: Vec<u8> = bincode::serialize(&section_header).unwrap();
        section_bytes.extend(&section_data);
        let padding_len = 8 - (section_bytes.len() % 8);
        let padding = vec![0; padding_len];
        section_bytes.extend(&padding);
        section_bytes.extend(bincode::serialize(&section_header_two).unwrap());
        section_bytes.extend(&section_data_two);

        container_header.length = u32::try_from(section_bytes.len()).unwrap();
        let mut zbi_bytes: Vec<u8> = bincode::serialize(&container_header).unwrap();
        zbi_bytes.extend(&section_bytes);

        let mut reader = ZbiReader::new(zbi_bytes);
        let sections = reader.parse().unwrap();
        assert_eq!(sections.len(), 2);
        assert_eq!(sections[0].buffer.len(), 7);
        assert_eq!(sections[1].buffer.len(), 10);
    }
}
