// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::zstd,
    anyhow::{Error, Result},
    byteorder::{LittleEndian, ReadBytesExt},
    hex,
    log::warn,
    serde::Serialize,
    std::cmp,
    std::convert::{TryFrom, TryInto},
    std::io::{Cursor, Read, Seek, SeekFrom},
    thiserror::Error,
};

/// Taken from //src/storage/blobfs/include/blobfs/format.h
const BLOBFS_MAGIC_0: u64 = 0xac2153479e694d21;
const BLOBFS_MAGIC_1: u64 = 0x985000d4d4d3d314;
const BLOBFS_VERSION: u32 = 8;
const BLOBFS_BLOCK_SIZE: u64 = 8192;
const BLOBFS_MERKLE_SIZE: u64 = 32;
const BLOBFS_BLOCK_BITS: u64 = BLOBFS_BLOCK_SIZE * 8;
const BLOBFS_FVM_FLAG: u32 = 4;
const BLOBFS_BLOCK_MAP_START: u64 = 1;
const BLOBFS_INODE_SIZE: u64 = 64;
const BLOBFS_INODE_PER_BLOCK: u64 = BLOBFS_BLOCK_SIZE / BLOBFS_INODE_SIZE;
const BLOBFS_FLAG_ALLOCATED: u16 = 1 << 0;
const BLOBFS_FLAG_LZ4_COMPRESSED: u16 = 1 << 1;
const BLOBFS_FLAG_EXTENT_CONTAINER: u16 = 1 << 2;
const BLOBFS_FLAG_ZSTD_COMPRESSED: u16 = 1 << 3;
const BLOBFS_FLAG_ZSTD_SEEK_COMPRESSED: u16 = 1 << 4;
const BLOBFS_FLAG_CHUNK_COMPRESSED: u16 = 1 << 5;
const BLOBFS_FLAG_MASK_ANY_COMPRESSION: u16 = BLOBFS_FLAG_LZ4_COMPRESSED
    | BLOBFS_FLAG_ZSTD_COMPRESSED
    | BLOBFS_FLAG_ZSTD_SEEK_COMPRESSED
    | BLOBFS_FLAG_CHUNK_COMPRESSED;
const BLOBFS_BLOCK_OFFSET_BITS: u64 = 48;
const BLOBFS_BLOCK_OFFSET_MAX: u64 = (1 << BLOBFS_BLOCK_OFFSET_BITS) - 1;
const BLOBFS_BLOCK_OFFSET_MASK: u64 = BLOBFS_BLOCK_OFFSET_MAX;
const BLOBFS_BLOCK_COUNT_MASK: u64 = 65535 << BLOBFS_BLOCK_OFFSET_BITS;

/// Defines the data held in the SuperBlock for the BlobFS filesystem. This
/// always occupies the first Block (8192 bytes) of the partition. BlobFS is a
/// FVM aware file system so has optional parameters in the header that only
/// apply if located inside FVM.
#[allow(dead_code)]
#[derive(Debug, Serialize)]
pub struct BlobFsHeader {
    magic_0: u64,
    magic_1: u64,
    version: u32,
    flags: u32,
    block_size: u32,
    reserved_1: u32,
    data_block_count: u64,
    journal_block_count: u64,
    inode_count: u64,
    alloc_block_count: u64,
    alloc_inode_count: u64,
    reserved_2: u64,
    // The following fields are only valid with (flags & kBlobFlagFVM)
    slice_size: u64,
    vslice_count: u64,
    abm_slices: u32,
    ino_slices: u32,
    dat_slices: u32,
    journal_slices: u32,
}

impl BlobFsHeader {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        let starting_pos: i64 = cursor.position().try_into()?;
        let magic_0: u64 = cursor.read_u64::<LittleEndian>()?;
        if magic_0 != BLOBFS_MAGIC_0 {
            return Err(Error::new(BlobFsError::InvalidHeaderMagic));
        }
        let magic_1: u64 = cursor.read_u64::<LittleEndian>()?;
        if magic_1 != BLOBFS_MAGIC_1 {
            return Err(Error::new(BlobFsError::InvalidHeaderMagic));
        }
        let version: u32 = cursor.read_u32::<LittleEndian>()?;
        if version != BLOBFS_VERSION {
            return Err(Error::new(BlobFsError::UnsupportedVersion));
        }
        let flags: u32 = cursor.read_u32::<LittleEndian>()?;
        let block_size: u32 = cursor.read_u32::<LittleEndian>()?;
        let reserved_1: u32 = cursor.read_u32::<LittleEndian>()?;
        let data_block_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let journal_block_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let inode_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let alloc_block_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let alloc_inode_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let reserved_2: u64 = cursor.read_u64::<LittleEndian>()?;
        // Fvm Fields
        let slice_size: u64 = cursor.read_u64::<LittleEndian>()?;
        let vslice_count: u64 = cursor.read_u64::<LittleEndian>()?;
        let abm_slices: u32 = cursor.read_u32::<LittleEndian>()?;
        let ino_slices: u32 = cursor.read_u32::<LittleEndian>()?;
        let dat_slices: u32 = cursor.read_u32::<LittleEndian>()?;
        let journal_slices: u32 = cursor.read_u32::<LittleEndian>()?;
        let end_pos: i64 = cursor.position().try_into()?;
        let header_len: i64 = end_pos - starting_pos;
        if header_len < BLOBFS_BLOCK_SIZE.try_into()? {
            let padding: i64 = i64::try_from(BLOBFS_BLOCK_SIZE)? - header_len;
            cursor.seek(SeekFrom::Current(padding))?;
        }

        Ok(Self {
            magic_0,
            magic_1,
            version,
            flags,
            block_size,
            reserved_1,
            data_block_count,
            journal_block_count,
            inode_count,
            alloc_block_count,
            alloc_inode_count,
            reserved_2,
            slice_size,
            vslice_count,
            abm_slices,
            ino_slices,
            dat_slices,
            journal_slices,
        })
    }

    /// Returns true if this blobfs partition is FVM aware.
    pub fn is_fvm(&self) -> bool {
        self.flags & BLOBFS_FVM_FLAG == BLOBFS_FVM_FLAG
    }

    /// Returns the block index where the block map starts.
    pub fn block_map_start_block(&self) -> u64 {
        if self.is_fvm() {
            self.slice_size / BLOBFS_BLOCK_SIZE
        } else {
            BLOBFS_BLOCK_MAP_START
        }
    }

    /// Returns the number of blocks in the block map.
    pub fn block_map_block_count(&self) -> u64 {
        if self.is_fvm() {
            (u64::from(self.abm_slices) * self.slice_size) / BLOBFS_BLOCK_SIZE
        } else {
            round_up(self.data_block_count, BLOBFS_BLOCK_BITS) / BLOBFS_BLOCK_BITS
        }
    }

    /// Returns the block index where the node map starts.
    pub fn node_map_start_block(&self) -> u64 {
        self.block_map_start_block() + self.block_map_block_count()
    }

    /// Returns the number of blocks in the node map.
    pub fn node_map_block_count(&self) -> u64 {
        if self.is_fvm() {
            (u64::from(self.ino_slices) * self.slice_size) / BLOBFS_BLOCK_SIZE
        } else {
            round_up(self.inode_count, BLOBFS_INODE_PER_BLOCK) / BLOBFS_INODE_PER_BLOCK
        }
    }

    /// Returns the block index where the journal starts.
    pub fn journal_start_block(&self) -> u64 {
        self.node_map_start_block() + self.node_map_block_count()
    }

    /// Returns the number of blocks in the journal.
    pub fn journal_block_count(&self) -> u64 {
        if self.is_fvm() {
            (u64::from(self.journal_slices) * self.slice_size) / BLOBFS_BLOCK_SIZE
        } else {
            self.journal_block_count
        }
    }

    /// Returns the block index where the data blocks start.
    pub fn data_blocks_start_block(&self) -> u64 {
        self.journal_start_block() + self.journal_block_count()
    }

    /// Returns the number of blocks in the data blocks.
    pub fn data_blocks_block_count(&self) -> u64 {
        if self.is_fvm() {
            (u64::from(self.dat_slices) * self.slice_size) / BLOBFS_BLOCK_SIZE
        } else {
            self.data_block_count
        }
    }
}

/// round_up value until its divisible by a multiple.
fn round_up(val: u64, mult: u64) -> u64 {
    if val == 0 {
        0
    } else {
        // is_pow2
        if (mult != 0) && (((val - 1) & val) == 0) {
            (val + (mult - 1)) & !(mult - 1)
        } else {
            ((val + (mult - 1)) / mult) * mult
        }
    }
}

/// Calculates the size of the hash list.
fn calculate_hash_list_size(data_size: u64, node_size: u64) -> u64 {
    let digest_size = 32;
    let next_align = round_up(data_size, node_size);
    let to_node = next_align / node_size;
    cmp::max(to_node * digest_size, digest_size)
}

/// Calculates the number of blocks needed to store the merkle tree for the blob
/// based on its size. This function may return 0 for small blobs (for which
/// only the root digest is sufficient to verify the entire contents of the blob).
fn merkle_tree_block_count(node: &Inode) -> u64 {
    let mut data_size: u64 = node.blob_size;
    let node_size: u64 = BLOBFS_BLOCK_SIZE;
    let mut merkle_tree_size: u64 = 0;
    while data_size > node_size {
        data_size = round_up(calculate_hash_list_size(data_size, node_size), node_size);
        merkle_tree_size += data_size;
    }
    round_up(merkle_tree_size, BLOBFS_BLOCK_SIZE) / BLOBFS_BLOCK_SIZE
}

/// All nodes in the NodeMap start with a header prelude which stores common
/// information and whether the node is an inode (start of a file) or an
/// extent container (a fragment of a file). This also critically annotates
/// whether the file is compressed, and if it is allocated.
#[allow(dead_code)]
#[derive(Debug, Serialize)]
pub struct NodePrelude {
    flags: u16,
    version: u16,
    next_node: u32,
}

impl NodePrelude {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        Ok(Self {
            flags: cursor.read_u16::<LittleEndian>()?,
            version: cursor.read_u16::<LittleEndian>()?,
            next_node: cursor.read_u32::<LittleEndian>()?,
        })
    }
    /// Returns whether the blob is allocated or not.
    pub fn is_allocated(&self) -> bool {
        self.flags & BLOBFS_FLAG_ALLOCATED == BLOBFS_FLAG_ALLOCATED
    }
    /// Returns whether the node is an extent container.
    pub fn is_extent_container(&self) -> bool {
        self.flags & BLOBFS_FLAG_EXTENT_CONTAINER == BLOBFS_FLAG_EXTENT_CONTAINER
    }
    /// Returns whether this node is an inode (start of a file).
    pub fn is_inode(&self) -> bool {
        !self.is_extent_container()
    }
    /// Returns true if the inode is lz4 compressed.
    pub fn is_lz4_compressed(&self) -> bool {
        self.flags & BLOBFS_FLAG_LZ4_COMPRESSED == BLOBFS_FLAG_LZ4_COMPRESSED
    }
    /// Returns whether the files data is compressed with ZSTD.
    pub fn is_zstd_compressed(&self) -> bool {
        self.flags & BLOBFS_FLAG_ZSTD_COMPRESSED == BLOBFS_FLAG_ZSTD_COMPRESSED
    }
    /// Returns whether the files data is compressed with ZSTD_SEEK.
    pub fn is_zstd_seek_compressed(&self) -> bool {
        self.flags & BLOBFS_FLAG_ZSTD_SEEK_COMPRESSED == BLOBFS_FLAG_ZSTD_SEEK_COMPRESSED
    }
    /// Returns true if the inode is chunk compressed.
    pub fn is_chunk_compressed(&self) -> bool {
        self.flags & BLOBFS_FLAG_CHUNK_COMPRESSED == BLOBFS_FLAG_CHUNK_COMPRESSED
    }
    /// Returns true if the inode is chunk compressed.
    pub fn is_compressed(&self) -> bool {
        self.flags & BLOBFS_FLAG_MASK_ANY_COMPRESSION != 0
    }
}

/// An Extent is simply an a 2-tuple of (DataBlockIndex, DataBlockLength) paired
/// into a u64 with masked offsets. A chain of extents form a file with each
/// extent representing a contiguous range of blocks within the data blocks
/// section of BlobFS. Combining the chain of extents together in order forms
/// a file. With no fragmentation all files can be represented with a single
/// extent (which is common in Scrutiny's use cases).
#[derive(Debug, Serialize)]
pub struct Extent {
    data: u64,
}

impl Extent {
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        Ok(Self { data: cursor.read_u64::<LittleEndian>()? })
    }

    /// The block index relative to the Data Blocks offset where this extent starts.
    pub fn start(&self) -> u64 {
        self.data & BLOBFS_BLOCK_OFFSET_MASK
    }

    /// The number of blocks this extent occupies the blocks from start->length
    /// represent the contiguous buffer of this extent.
    pub fn length(&self) -> u64 {
        (self.data & BLOBFS_BLOCK_COUNT_MASK) >> BLOBFS_BLOCK_OFFSET_BITS
    }
}

/// Represents the start of a file in BlobFS. Fragmented files will use the
/// NodePrelude to point to additional ExtentContainers which form a link list
/// of (indexes, length) pairs into the data block section. As an optimization
/// the Inode always includes the first extent which with no fragmentation is
/// often the only extent required to define the file.
#[allow(dead_code)]
#[derive(Serialize, Debug)]
pub struct Inode {
    merkle_root_hash: Vec<u8>,
    blob_size: u64,
    block_count: u32,
    extent_count: u16,
    reserved: u16,
    inline_extent: Extent,
}

impl Inode {
    /// Parses the Inode and the inline extent. This can only fail if the cursor
    /// reaches EOF before the expected 32 bytes have been read.
    pub fn parse(cursor: &mut Cursor<Vec<u8>>) -> Result<Self> {
        let mut merkle_root_hash = vec![0u8; BLOBFS_MERKLE_SIZE as usize];
        cursor.read_exact(&mut merkle_root_hash)?;
        let blob_size: u64 = cursor.read_u64::<LittleEndian>()?;
        let block_count: u32 = cursor.read_u32::<LittleEndian>()?;
        let extent_count: u16 = cursor.read_u16::<LittleEndian>()?;
        let reserved: u16 = cursor.read_u16::<LittleEndian>()?;
        let inline_extent = Extent::parse(cursor)?;
        Ok(Inode {
            merkle_root_hash,
            blob_size,
            block_count,
            extent_count,
            reserved,
            inline_extent,
        })
    }
}

#[allow(dead_code)]
pub struct ExtentContainer {
    previous_node: u32,
    extent_count: u16,
    reserved: u16,
    extents: Vec<Extent>, // With a max of BLOBFS_CONTAINER_MAX_EXTENTS
}

#[derive(Debug)]
pub struct Blob {
    pub merkle: String,
    pub buffer: Vec<u8>,
}

#[derive(Error, Debug, PartialEq, Eq)]
pub enum BlobFsError {
    #[error("Blobfs header magic value doesn't match expected value")]
    InvalidHeaderMagic,
    #[error("Blobfs header version is not supported")]
    UnsupportedVersion,
}

pub struct BlobFsReader {
    cursor: Cursor<Vec<u8>>,
}

impl BlobFsReader {
    /// Constructs a new reader from an existing fvm_buffer.
    pub fn new(fvm_buffer: Vec<u8>) -> Self {
        Self { cursor: Cursor::new(fvm_buffer) }
    }

    /// Parses the FVM provided during construction returning the set of
    /// partitions as buffers ordered by vslice.
    pub fn parse(&mut self) -> Result<Vec<Blob>> {
        let header = BlobFsHeader::parse(&mut self.cursor)?;
        // Seek to the start of the node map and scan through the entire map
        // for all allocated inodes (and their extents). Note each inode or
        // extent is fixed at 256 bytes (32 per block). So we are scanning
        // one at a time.
        let node_map_offset = header.node_map_start_block() * BLOBFS_BLOCK_SIZE;
        let data_offset = header.data_blocks_start_block() * BLOBFS_BLOCK_SIZE;

        let mut blobs = Vec::new();
        for i in 0..header.inode_count {
            self.cursor.seek(SeekFrom::Start(node_map_offset))?;
            self.cursor.seek(SeekFrom::Current((i * BLOBFS_INODE_SIZE).try_into().unwrap()))?;
            let prelude = NodePrelude::parse(&mut self.cursor)?;

            // We only care about allocated files.
            if prelude.is_inode() && prelude.is_allocated() {
                let inode = Inode::parse(&mut self.cursor)?;
                let merkle = hex::encode(inode.merkle_root_hash.clone());

                if prelude.next_node != 0 {
                    warn!("Next node not supported: {}", merkle);
                    continue;
                }
                if inode.extent_count > 1 {
                    warn!("Extended containers are not currently supported: {}", merkle);
                    continue;
                }
                if prelude.is_compressed() && !prelude.is_chunk_compressed() {
                    warn!("This type of compression isn't supported for: {}", merkle);
                    continue;
                }

                // Skip forward to the data block map and read the data.
                self.cursor.seek(SeekFrom::Start(data_offset))?;
                self.cursor.seek(SeekFrom::Current(
                    (inode.inline_extent.start() * BLOBFS_BLOCK_SIZE).try_into().unwrap(),
                ))?;

                // Skip over the merkle tree blocks.
                let merkle_tree_size = merkle_tree_block_count(&inode) * BLOBFS_BLOCK_SIZE;
                self.cursor.seek(SeekFrom::Current(merkle_tree_size.try_into().unwrap()))?;

                // Read the data blocks directly, accounting for the space taken up by the merkle
                // tree.
                let data_length =
                    (inode.inline_extent.length() * BLOBFS_BLOCK_SIZE) - merkle_tree_size;
                let mut buffer = vec![0u8; data_length as usize];
                self.cursor.read_exact(&mut buffer)?;

                // Perform zstd chunk decompression if required.
                if prelude.is_chunk_compressed() {
                    let mut decompressed =
                        zstd::chunked_decompress(&buffer, inode.blob_size.try_into().unwrap());
                    if decompressed.len() != 0 {
                        decompressed.truncate(inode.blob_size as usize);
                        blobs.push(Blob { merkle, buffer: decompressed });
                    } else {
                        warn!("Failed to decompress: {}", merkle);
                    }
                } else {
                    buffer.truncate(inode.blob_size as usize);
                    blobs.push(Blob { merkle, buffer });
                }
            }
        }
        Ok(blobs)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fake_blobfs_header() -> BlobFsHeader {
        BlobFsHeader {
            magic_0: BLOBFS_MAGIC_0,
            magic_1: BLOBFS_MAGIC_1,
            version: BLOBFS_VERSION,
            flags: 0,
            block_size: 8192,
            reserved_1: 0,
            data_block_count: 8,
            journal_block_count: 1,
            inode_count: 2,
            alloc_block_count: 8,
            alloc_inode_count: 2,
            reserved_2: 0,
            // The following fields are only valid with (flags & kBlobFlagFVM)
            slice_size: 0,
            vslice_count: 0,
            abm_slices: 0,
            ino_slices: 0,
            dat_slices: 0,
            journal_slices: 0,
        }
    }

    fn fake_prelude() -> NodePrelude {
        NodePrelude { flags: BLOBFS_FLAG_ALLOCATED, version: 1, next_node: 0 }
    }

    fn fake_inode() -> Inode {
        Inode {
            merkle_root_hash: vec![0u8; 32],
            blob_size: 64,
            block_count: 2,
            extent_count: 1,
            reserved: 0,
            // Maps to one extent at offset one.
            inline_extent: Extent { data: 281474976710657 },
        }
    }

    #[test]
    fn test_blobfs_empty_invalid() {
        let blobfs_bytes = vec![0u8];
        let mut reader = BlobFsReader::new(blobfs_bytes);
        let result = reader.parse();
        assert_eq!(result.is_err(), true);
    }

    #[test]
    fn test_round_up() {
        assert_eq!(round_up(10, 64), 64);
        assert_eq!(round_up(100, 64), 128);
        assert_eq!(round_up(128, 128), 128);
        assert_eq!(round_up(129, 128), 256);
        assert_eq!(round_up(0, 128), 0);
        assert_eq!(round_up(5, 1), 5);
    }

    #[test]
    fn test_calculate_hash_list_size() {
        assert_eq!(calculate_hash_list_size(32, 32), 32);
        assert_eq!(calculate_hash_list_size(64, 64), 32);
        assert_eq!(calculate_hash_list_size(128, 128), 32);
        assert_eq!(calculate_hash_list_size(256, 256), 32);
        assert_eq!(calculate_hash_list_size(512, 512), 32);
        assert_eq!(calculate_hash_list_size(512, 512), 32);
        assert_eq!(calculate_hash_list_size(1024, 1024), 32);
        assert_eq!(calculate_hash_list_size(2048, 2048), 32);
        assert_eq!(calculate_hash_list_size(4096, 4096), 32);
        assert_eq!(calculate_hash_list_size(8192, 8192), 32);
        assert_eq!(calculate_hash_list_size(81920, 8192), 320);
    }

    #[test]
    fn test_merkle_tree_block_count() {
        let inode_0 = Inode {
            merkle_root_hash: vec![],
            blob_size: 1024,
            block_count: 2,
            extent_count: 1,
            reserved: 0,
            inline_extent: Extent { data: 100 },
        };
        assert_eq!(merkle_tree_block_count(&inode_0), 0);

        let inode_1 = Inode {
            merkle_root_hash: vec![],
            blob_size: 8193,
            block_count: 3,
            extent_count: 1,
            reserved: 0,
            inline_extent: Extent { data: 100 },
        };
        assert_eq!(merkle_tree_block_count(&inode_1), 1);

        let inode_2 = Inode {
            merkle_root_hash: vec![],
            blob_size: 8192000,
            block_count: 3,
            extent_count: 1,
            reserved: 0,
            inline_extent: Extent { data: 100 },
        };
        assert_eq!(merkle_tree_block_count(&inode_2), 5);
    }

    #[test]
    fn test_blobfs_reader_bad_magic_value() {
        let mut header = fake_blobfs_header();
        header.magic_0 = 0;
        let blobfs_bytes = bincode::serialize(&header).unwrap();
        let mut reader = BlobFsReader::new(blobfs_bytes);
        let result = reader.parse();
        assert_eq!(
            result.unwrap_err().downcast::<BlobFsError>().unwrap(),
            BlobFsError::InvalidHeaderMagic
        );
    }

    #[test]
    fn test_blobfs_reader_bad_version_value() {
        let mut header = fake_blobfs_header();
        header.version = 500;
        let blobfs_bytes = bincode::serialize(&header).unwrap();
        let mut reader = BlobFsReader::new(blobfs_bytes);
        let result = reader.parse();
        assert_eq!(
            result.unwrap_err().downcast::<BlobFsError>().unwrap(),
            BlobFsError::UnsupportedVersion
        );
    }

    #[test]
    fn test_blobfs_no_allocations() {
        let header = fake_blobfs_header();
        let mut blobfs_bytes = bincode::serialize(&header).unwrap();
        let mut empty_data = vec![0u8; 8192 * 20];
        blobfs_bytes.append(&mut empty_data);
        let mut reader = BlobFsReader::new(blobfs_bytes);
        let blobs = reader.parse().unwrap();
        assert_eq!(blobs.len(), 0);
    }

    #[test]
    fn test_blobfs_allocations() {
        let block_size: usize = 8192;
        let header = fake_blobfs_header();
        let mut blobfs_bytes = bincode::serialize(&header).unwrap();
        let padding_size = block_size - blobfs_bytes.len();
        let mut padding = vec![0u8; padding_size];
        blobfs_bytes.append(&mut padding);
        let mut block_map_bytes = vec![0u8; block_size];
        blobfs_bytes.append(&mut block_map_bytes);
        let prelude = fake_prelude();
        let mut prelude_bytes = bincode::serialize(&prelude).unwrap();
        let inode = fake_inode();
        let mut inode_bytes = bincode::serialize(&inode).unwrap();
        let node_padding_size = block_size - prelude_bytes.len() - inode_bytes.len();
        blobfs_bytes.append(&mut prelude_bytes);
        blobfs_bytes.append(&mut inode_bytes);
        let mut node_padding = vec![0u8; node_padding_size];
        blobfs_bytes.append(&mut node_padding);
        let mut journal_map_bytes = vec![0u8; block_size];
        blobfs_bytes.append(&mut journal_map_bytes);
        let mut data_blocks = vec![0u8; 5 * block_size];
        blobfs_bytes.append(&mut data_blocks);
        // Verify we get a blob.
        let mut reader = BlobFsReader::new(blobfs_bytes);
        let blobs = reader.parse().unwrap();
        assert_eq!(blobs.len(), 1);
    }
}
