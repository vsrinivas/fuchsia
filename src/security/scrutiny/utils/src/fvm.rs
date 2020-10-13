// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    byteorder::{LittleEndian, ReadBytesExt},
    log::info,
    serde::{Deserialize, Serialize},
    std::collections::HashMap,
    std::convert::{TryFrom, TryInto},
    std::fmt,
    std::io::{Cursor, Read, Seek, SeekFrom},
    std::str,
    thiserror::Error,
};

const FVM_MAGIC: u64 = 0x54524150204d5646;
const FVM_VERSION: u64 = 0x00000001;
const FVM_BLOCK_SIZE: u64 = 8192;
const FVM_MAX_VPARTITION_NAME_LEN: usize = 24;
const FVM_MAX_VPARTITIONS: usize = 1024;
const SHA256_BYTE_LEN: usize = 32;
const GPT_GUID_LEN: usize = 16;
// Masks and constants required to extract the opaque slice entry data.
const SLICE_ENTRY_VPARTITION_BITS: u64 = 16;
const SLICE_ENTRY_VSLICE_BITS: u64 = 32;
const SLICE_ENTRY_VSLICE_MAX: u64 = (1 << SLICE_ENTRY_VSLICE_BITS) - 1;
const SLICE_ENTRY_VSLICE_MASK: u64 = SLICE_ENTRY_VSLICE_MAX << SLICE_ENTRY_VPARTITION_BITS;
const VPARTITION_ENTRY_MASK: u64 = (1 << SLICE_ENTRY_VPARTITION_BITS) - 1;

/// The set of supported FVM partition types.
#[derive(Debug, PartialEq, Eq)]
pub enum FvmPartitionType {
    MinFs,
    BlobFs,
}

impl fmt::Display for FvmPartitionType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match &self {
            FvmPartitionType::MinFs => write!(f, "minfs"),
            FvmPartitionType::BlobFs => write!(f, "blobfs"),
        }
    }
}

/// The FvmPartition represents a contiguous typed partition. The buffer
/// represents the entire set of vslices for the partition merged into a single
/// buffer. This representation is obviously memory intensive but is currently
/// designed for small buffers less than 1GB.
#[derive(Debug)]
pub struct FvmPartition {
    pub partition_type: FvmPartitionType,
    pub buffer: Vec<u8>,
}

/// Defines the FvmHeader for a particular partition. In this case we are
/// parsing the structure using a cursor so this structure doesn't have to
/// directly match the underlying type.
#[derive(Serialize, Deserialize, Clone, PartialEq, Eq)]
struct FvmHeader {
    magic: u64,
    version: u64,
    pslice_count: u64,
    slice_size: u64,
    fvm_partition_size: u64, // Total size of this partition.
    vpartition_table_size: u64,
    allocation_table_size: u64,
    generation: u64,
    hash: Vec<u8>,
}

impl FvmHeader {
    /// Parses the entire FVM header from a binary cursor. This function will
    /// error if either the cursor ends before it is expected to or the magic
    /// number provided in the header is incorrect.
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        let starting_pos: i64 = cursor.position().try_into()?;
        let magic: u64 = cursor.read_u64::<LittleEndian>()?;
        if magic != FVM_MAGIC {
            return Err(Error::new(FvmError::InvalidHeaderMagic));
        }
        let version: u64 = cursor.read_u64::<LittleEndian>()?;
        if version != FVM_VERSION {
            return Err(Error::new(FvmError::UnsupportedVersion));
        }
        let pslice_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let slice_size: u64 = cursor.read_u64::<LittleEndian>()?;
        let fvm_partition_size: u64 = cursor.read_u64::<LittleEndian>()?;
        let vpartition_table_size: u64 = cursor.read_u64::<LittleEndian>()?;
        let allocation_table_size: u64 = cursor.read_u64::<LittleEndian>()?;
        let generation: u64 = cursor.read_u64::<LittleEndian>()?;
        let mut hash = vec![0u8; SHA256_BYTE_LEN];
        cursor.read_exact(&mut hash)?;

        // The FVM Header sits in a superblock so is padded for FVM_BLOCK_SIZE
        // at the end of the structure.
        let ending_pos: i64 = cursor.position().try_into()?;
        let header_len: i64 = ending_pos - starting_pos;
        if header_len < FVM_BLOCK_SIZE.try_into()? {
            let padding: i64 = i64::try_from(FVM_BLOCK_SIZE)? - header_len;
            cursor.seek(SeekFrom::Current(padding))?;
        }

        Ok(Self {
            magic,
            version,
            pslice_count,
            slice_size,
            fvm_partition_size,
            vpartition_table_size,
            allocation_table_size,
            generation,
            hash,
        })
    }
}

/// Represents an entry in the FVM partition table which is a fixed contiguous
/// flat buffer.
#[allow(dead_code)]
pub struct VPartitionEntry {
    partition_type: Vec<u8>,
    guid: Vec<u8>,
    slices: u32,
    flags: u32,
    unsafe_name: Vec<u8>,
}

impl VPartitionEntry {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        let mut partition_type = vec![0u8; GPT_GUID_LEN];
        cursor.read_exact(&mut partition_type)?;
        let mut guid = vec![0u8; GPT_GUID_LEN];
        cursor.read_exact(&mut guid)?;
        let slices: u32 = cursor.read_u32::<LittleEndian>()?;
        let flags: u32 = cursor.read_u32::<LittleEndian>()?;
        let mut unsafe_name = vec![0u8; FVM_MAX_VPARTITION_NAME_LEN];
        cursor.read_exact(&mut unsafe_name)?;
        Ok(Self { partition_type, guid, slices, flags, unsafe_name })
    }
}

#[allow(dead_code)]
pub struct SliceEntry {
    data: u64,
}

impl SliceEntry {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        Ok(Self { data: cursor.read_u64::<LittleEndian>()? })
    }

    pub fn is_allocated(&self) -> bool {
        self.vpartition() != 0
    }

    pub fn vslice(&self) -> u64 {
        (self.data & SLICE_ENTRY_VSLICE_MASK) >> SLICE_ENTRY_VPARTITION_BITS
    }

    pub fn vpartition(&self) -> u64 {
        self.data & VPARTITION_ENTRY_MASK
    }
}

#[derive(Error, Debug, PartialEq, Eq)]
pub enum FvmError {
    #[error("Fvm header magic value doesn't match expected value")]
    InvalidHeaderMagic,
    #[error("Fvm header version is not supported")]
    UnsupportedVersion,
}

/// The FvmReader parses the SuperBlock which fits inside a single block.
/// Following the SuperBlock is the Virtual Partition Table followed directly
/// by the Allocation Table. The reader will return a vector of FvmPartitions
/// which contain a filesystem type and the "virtual" representation of the
/// partition (sorted pslices by vslice).
pub struct FvmReader {
    cursor: Cursor<Vec<u8>>,
}

impl FvmReader {
    /// Constructs a new reader from an existing fvm_buffer.
    pub fn new(fvm_buffer: Vec<u8>) -> Self {
        Self { cursor: Cursor::new(fvm_buffer) }
    }

    /// Parses the FVM provided during construction returning the set of
    /// partitions as buffers ordered by vslice.
    pub fn parse(&mut self) -> Result<Vec<FvmPartition>> {
        // Parse the SuperBlock header which always fits inside one block.
        let header = FvmHeader::parse(&mut self.cursor)?;
        // Read the partition table that directly follows the super block.
        let mut partitions = Vec::with_capacity(FVM_MAX_VPARTITIONS);
        for _i in 0..FVM_MAX_VPARTITIONS {
            partitions.push(VPartitionEntry::parse(&mut self.cursor)?);
        }
        // The actual number of entries in the table.
        let allocation_entries = header.fvm_partition_size / header.slice_size;
        // The number of bits total that the entire allocation contains.
        let total_allocation_entries = header.allocation_table_size / 8;
        let padding_entries = total_allocation_entries - allocation_entries;

        // Load in all the slice allocations from the alloctaion table.
        let mut allocations = Vec::with_capacity(allocation_entries.try_into()?);
        for _i in 0..allocation_entries {
            let slice = SliceEntry::parse(&mut self.cursor)?;
            allocations.push(slice);
        }
        // Push the cursor to the end of the block, ignore these allocation
        // entries they are just padding.
        for _i in 0..padding_entries {
            let _ = SliceEntry::parse(&mut self.cursor);
        }
        // Calculate mapping from partition_id -> (vslice, allocation_index)
        let mut partition_alloc_map: HashMap<u64, Vec<(u64, u64)>> = HashMap::new();
        let mut allocation_index = 0;
        for slice in allocations {
            if slice.is_allocated() {
                if !partition_alloc_map.contains_key(&slice.vpartition()) {
                    partition_alloc_map
                        .insert(slice.vpartition(), vec![(slice.vslice(), allocation_index)]);
                } else {
                    partition_alloc_map
                        .get_mut(&slice.vpartition())
                        .unwrap()
                        .push((slice.vslice(), allocation_index));
                }
            }
            allocation_index += 1;
        }
        // Sort the allocations by vslice 0 -> max, this uses the property that
        // 2-tuple pairs are first sorted by their 1st param then their second.
        for (_, v) in partition_alloc_map.iter_mut() {
            v.sort();
        }

        // Convert the internal fragmented pslice allocation format to a
        // simple contiguous buffer. This is the "virtual" representation per
        // partition as informed by the sorted partition_alloc_map.
        let mut fvm_partitions = vec![];
        // Two copies of the metadata are always stored at the start of the FVM.
        let slice_section_start = 2 * self.cursor.position();
        self.cursor.seek(SeekFrom::Start(slice_section_start))?;

        for (partition_index, vslice_pslice_mappings) in partition_alloc_map.iter() {
            let idx = partition_index.clone() as usize;
            let idx = usize::try_from(idx)?;
            if let Ok(name) = str::from_utf8(&partitions[idx].unsafe_name) {
                let fs_type = if name.starts_with("blobfs") {
                    info!("FVM: Found BlobFS volume with {} slices", vslice_pslice_mappings.len());
                    Some(FvmPartitionType::BlobFs)
                } else if name.starts_with("minfs") {
                    info!("FVM: Found Minfs volume with {} slices", vslice_pslice_mappings.len());
                    Some(FvmPartitionType::MinFs)
                } else {
                    None
                };

                // Only create FVM file systems from known types.
                if let Some(fs_type) = fs_type {
                    // Note that the mappings are in sorted order already.
                    let mut partition_buffer = vec![];
                    for (vslice, pslice) in vslice_pslice_mappings.iter() {
                        self.cursor.seek(SeekFrom::Start(slice_section_start))?;
                        let offset = i64::try_from((pslice - 1) * header.slice_size)?;
                        info!(
                            "Seeking: {} {} {}",
                            vslice,
                            pslice,
                            slice_section_start + (pslice - 1) * header.slice_size
                        );
                        self.cursor.seek(SeekFrom::Current(offset))?;
                        let mut slice_buffer = vec![1; header.slice_size as usize];
                        self.cursor.read(&mut slice_buffer)?;
                        partition_buffer.append(&mut slice_buffer);
                    }
                    fvm_partitions
                        .push(FvmPartition { partition_type: fs_type, buffer: partition_buffer });
                }
            }
        }

        Ok(fvm_partitions)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::convert::TryFrom};

    #[test]
    fn test_fvm_magic_invalid() {
        let header_invalid_magic = FvmHeader {
            magic: 1,
            version: FVM_VERSION,
            pslice_count: 1,
            slice_size: FVM_BLOCK_SIZE,
            fvm_partition_size: FVM_BLOCK_SIZE,
            vpartition_table_size: FVM_BLOCK_SIZE,
            allocation_table_size: FVM_BLOCK_SIZE,
            generation: 0,
            hash: vec![0u8; GPT_GUID_LEN],
        };
        let fvm_bytes = bincode::serialize(&header_invalid_magic).unwrap();
        let mut reader = FvmReader::new(fvm_bytes);
        let result = reader.parse();
        assert_eq!(
            result.unwrap_err().downcast::<FvmError>().unwrap(),
            FvmError::InvalidHeaderMagic
        );
    }

    #[test]
    fn test_fvm_version_invalid() {
        let header_invalid_magic = FvmHeader {
            magic: FVM_MAGIC,
            version: 123,
            pslice_count: 1,
            slice_size: FVM_BLOCK_SIZE,
            fvm_partition_size: FVM_BLOCK_SIZE,
            vpartition_table_size: FVM_BLOCK_SIZE,
            allocation_table_size: FVM_BLOCK_SIZE,
            generation: 0,
            hash: vec![0u8; GPT_GUID_LEN],
        };
        let fvm_bytes = bincode::serialize(&header_invalid_magic).unwrap();
        let mut reader = FvmReader::new(fvm_bytes);
        let result = reader.parse();
        assert_eq!(
            result.unwrap_err().downcast::<FvmError>().unwrap(),
            FvmError::UnsupportedVersion
        );
    }

    #[test]
    fn test_fvm_truncated_header() {
        let header_invalid_magic = FvmHeader {
            magic: FVM_MAGIC,
            version: FVM_VERSION,
            pslice_count: 1,
            slice_size: FVM_BLOCK_SIZE,
            fvm_partition_size: FVM_BLOCK_SIZE,
            vpartition_table_size: FVM_BLOCK_SIZE,
            allocation_table_size: FVM_BLOCK_SIZE,
            generation: 0,
            hash: vec![0u8; GPT_GUID_LEN],
        };
        let mut fvm_bytes = bincode::serialize(&header_invalid_magic).unwrap();
        fvm_bytes.pop();
        let mut reader = FvmReader::new(fvm_bytes);
        let result = reader.parse();
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_fvm_empty() {
        let header_invalid_magic = FvmHeader {
            magic: FVM_MAGIC,
            version: FVM_VERSION,
            pslice_count: 1,
            slice_size: FVM_BLOCK_SIZE,
            fvm_partition_size: FVM_BLOCK_SIZE,
            vpartition_table_size: FVM_BLOCK_SIZE,
            allocation_table_size: FVM_BLOCK_SIZE,
            generation: 0,
            hash: vec![0u8; GPT_GUID_LEN],
        };
        let mut fvm_bytes = bincode::serialize(&header_invalid_magic).unwrap();
        let mut padding = vec![0u8; usize::try_from(FVM_BLOCK_SIZE * 32).unwrap()];
        fvm_bytes.append(&mut padding);
        let mut reader = FvmReader::new(fvm_bytes);
        let result = reader.parse();
        assert_eq!(result.is_ok(), true);
    }
}
