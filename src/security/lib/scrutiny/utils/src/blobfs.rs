// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        fs::TemporaryDirectory,
        io::{ReadSeek, TryClone, WrappedReaderSeeker},
        zstd,
    },
    anyhow::{anyhow, Context, Error, Result},
    byteorder::{LittleEndian, ReadBytesExt},
    hex,
    serde::Serialize,
    std::cmp,
    std::convert::{TryFrom, TryInto},
    std::{
        collections::HashMap,
        fs::File,
        io::{BufReader, Read, Seek, SeekFrom},
        path::{Path, PathBuf},
        sync::Arc,
    },
    thiserror::Error,
    tracing::warn,
};

/// Taken from //src/storage/blobfs/include/blobfs/format.h
const BLOBFS_MAGIC_0: u64 = 0xac2153479e694d21;
const BLOBFS_MAGIC_1: u64 = 0x985000d4d4d3d314;
const BLOBFS_VERSION_8: u32 = 8;
const BLOBFS_VERSION_9: u32 = 9;
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
    pub fn parse<RS: Read + Seek>(reader: &mut RS) -> Result<Self> {
        let starting_pos: i64 = reader.stream_position()?.try_into()?;
        let magic_0: u64 = reader.read_u64::<LittleEndian>()?;
        if magic_0 != BLOBFS_MAGIC_0 {
            return Err(Error::new(BlobFsError::InvalidHeaderMagic));
        }
        let magic_1: u64 = reader.read_u64::<LittleEndian>()?;
        if magic_1 != BLOBFS_MAGIC_1 {
            return Err(Error::new(BlobFsError::InvalidHeaderMagic));
        }
        let version: u32 = reader.read_u32::<LittleEndian>()?;
        // BlobFS version 8 supports ZSTD_Seekable.
        // BlobFS version 9 removes support fo ZSTD_Seekable.
        // Scrutiny only supports ZSTD_CHUNK. So both versions are suitable
        // for use with the tool.
        if version != BLOBFS_VERSION_8 && version != BLOBFS_VERSION_9 {
            return Err(Error::new(BlobFsError::UnsupportedVersion));
        }

        let flags: u32 = reader.read_u32::<LittleEndian>()?;
        let block_size: u32 = reader.read_u32::<LittleEndian>()?;
        let reserved_1: u32 = reader.read_u32::<LittleEndian>()?;
        let data_block_count: u64 = reader.read_u64::<LittleEndian>()?;
        let journal_block_count: u64 = reader.read_u64::<LittleEndian>()?;
        let inode_count: u64 = reader.read_u64::<LittleEndian>()?;
        let alloc_block_count: u64 = reader.read_u64::<LittleEndian>()?;
        let alloc_inode_count: u64 = reader.read_u64::<LittleEndian>()?;
        let reserved_2: u64 = reader.read_u64::<LittleEndian>()?;
        // Fvm Fields
        let slice_size: u64 = reader.read_u64::<LittleEndian>()?;
        let vslice_count: u64 = reader.read_u64::<LittleEndian>()?;
        let abm_slices: u32 = reader.read_u32::<LittleEndian>()?;
        let ino_slices: u32 = reader.read_u32::<LittleEndian>()?;
        let dat_slices: u32 = reader.read_u32::<LittleEndian>()?;
        let journal_slices: u32 = reader.read_u32::<LittleEndian>()?;
        let end_pos: i64 = reader.stream_position()?.try_into()?;
        let header_len: i64 = end_pos - starting_pos;
        if header_len < BLOBFS_BLOCK_SIZE.try_into()? {
            let padding: i64 = i64::try_from(BLOBFS_BLOCK_SIZE)? - header_len;
            reader.seek(SeekFrom::Current(padding))?;
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

/// Returns the exact byte count for the merkle tree. This is used when a
/// compact merkle tree format is used in Version 9+ of the BlobFS format.
fn merkle_tree_byte_size(node: &Inode, is_compact: bool) -> u64 {
    let mut data_size: u64 = node.blob_size;
    let node_size: u64 = BLOBFS_BLOCK_SIZE;
    let mut merkle_tree_size: u64 = 0;
    while data_size > node_size {
        let list_size = calculate_hash_list_size(data_size, node_size);
        // The non compact format pads the hash list to be a multiple of the node size.
        data_size = if is_compact { list_size } else { round_up(list_size, node_size) };
        merkle_tree_size += data_size;
    }
    merkle_tree_size
}

/// Calculates the number of blocks needed to store the merkle tree for the blob
/// based on its size. This function may return 0 for small blobs (for which
/// only the root digest is sufficient to verify the entire contents of the blob).
fn merkle_tree_block_count(node: &Inode) -> u64 {
    let merkle_tree_size = merkle_tree_byte_size(node, false);
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
    pub fn parse<RS: Read + Seek>(cursor: &mut RS) -> Result<Self> {
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
    pub fn parse<RS: Read + Seek>(cursor: &mut RS) -> Result<Self> {
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
    pub fn parse<RS: Read + Seek>(reader: &mut RS) -> Result<Self> {
        let mut merkle_root_hash = vec![0u8; BLOBFS_MERKLE_SIZE as usize];
        reader.read_exact(&mut merkle_root_hash)?;
        let blob_size: u64 = reader.read_u64::<LittleEndian>()?;
        let block_count: u32 = reader.read_u32::<LittleEndian>()?;
        let extent_count: u16 = reader.read_u16::<LittleEndian>()?;
        let reserved: u16 = reader.read_u16::<LittleEndian>()?;
        let inline_extent = Extent::parse(reader)?;
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

/// Metadata about a blob loaded from a blobfs archive. Blob data is located in
/// the blobfs archive at `[data_start..dat_start+data_length]`.
#[derive(Debug)]
struct BlobMetadata {
    prelude: NodePrelude,
    inode: Inode,
    /// Absolute offset in blobfs archive to first byte of blob content. This is
    /// included in addition to prelude and inode data because it depends on the
    /// blobfs version located in the archive header.
    data_start: u64,
    /// Computed blob data length based on inode blob size and space occupied
    /// by the blob merkle tree. This value represents the number of content
    /// bytes in blobfs, regardless of whether those bytes are a compressed
    /// representation of the blob returned by the blobfs API.
    data_length: u64,
}

#[derive(Debug, Error, Eq, PartialEq)]
pub enum BlobFsError {
    #[error("Blobfs header magic value doesn't match expected value")]
    InvalidHeaderMagic,
    #[error("Blobfs header version is not supported")]
    UnsupportedVersion,
}

#[derive(Debug, Eq, PartialEq)]
pub enum BlobFsVersion {
    Version8,
    Version9,
}

/// A builder pattern implementation for`BlobFsReader`. Incremental builder
/// operations consume the receiver and return a `Result<Self>` to facilitate
/// `builder.op1(...)?.op2(...)?.build()?` patterns.
pub struct BlobFsReaderBuilder<TCRS: TryClone + Read + Seek> {
    reader_seeker: Option<TCRS>,
    tmp_dir: Option<Arc<TemporaryDirectory>>,
}

impl<TCRS: TryClone + Read + Seek + Send + Sync> BlobFsReaderBuilder<TCRS> {
    /// Construct an empty `BlobFsReaderBuilder`.
    pub fn new() -> Self {
        Self { reader_seeker: None, tmp_dir: None }
    }

    /// Set the blobfs archive that should be read by the built `BlobFsReader`.
    pub fn archive(mut self, reader_seeker: TCRS) -> Result<Self> {
        self.reader_seeker = Some(reader_seeker);
        Ok(self)
    }

    /// Set the blobfs archive that should be read by the built `BlobFsReader`.
    pub fn tmp_dir(mut self, tmp_dir: Arc<TemporaryDirectory>) -> Result<Self> {
        self.tmp_dir = Some(tmp_dir);
        Ok(self)
    }

    /// Build a `BlobFsReader` by parsing all metadata in the blobfs archive and
    /// injecting the metadata into a new `BlobFsReader` instance.
    pub fn build(self) -> Result<BlobFsReader<TCRS>> {
        let mut reader_seeker = self
            .reader_seeker
            .ok_or_else(|| anyhow!("Attempt to build blobfs reader with no archive reader"))?;
        let tmp_dir = self
            .tmp_dir
            .ok_or_else(|| anyhow!("Attempt to build blobfs reader with no temporary directory"))?;
        let header = BlobFsHeader::parse(&mut reader_seeker)?;
        let metadata = Arc::new(Self::parse_metadata(header, &mut reader_seeker)?);
        Ok(BlobFsReader { reader_seeker, tmp_dir, metadata })
    }

    fn parse_metadata(
        header: BlobFsHeader,
        reader: &mut TCRS,
    ) -> Result<HashMap<PathBuf, BlobMetadata>> {
        let blobfs_version = match header.version {
            BLOBFS_VERSION_8 => BlobFsVersion::Version8,
            BLOBFS_VERSION_9 => BlobFsVersion::Version9,
            _ => {
                return Err(anyhow!("Unsupported blobfs version: {}", header.version));
            }
        };
        let node_map_offset = header.node_map_start_block() * BLOBFS_BLOCK_SIZE;
        let data_offset = header.data_blocks_start_block() * BLOBFS_BLOCK_SIZE;

        let mut metadata = HashMap::new();
        for i in 0..header.inode_count {
            reader.seek(SeekFrom::Start(node_map_offset))?;
            reader.seek(SeekFrom::Current((i * BLOBFS_INODE_SIZE).try_into().unwrap()))?;
            let prelude = NodePrelude::parse(reader)?;

            // We only care about allocated files.
            if prelude.is_inode() && prelude.is_allocated() {
                let inode = Inode::parse(reader)?;
                let merkle = PathBuf::from(hex::encode(inode.merkle_root_hash.clone()));

                if inode.extent_count > 1 {
                    warn!(
                        ?merkle,
                        "Skipping blobfs blob. Extended containers are not currently supported",
                    );
                    continue;
                }
                if prelude.is_compressed() && !prelude.is_chunk_compressed() {
                    warn!(?merkle, "Skipping blobfs blob. Unsupported compression type");
                    continue;
                }

                // Compute offset beyond `data_offset` where merkle and data are stored.
                let extent_offset =
                    inode.inline_extent.start().checked_mul(BLOBFS_BLOCK_SIZE).ok_or_else(
                        || {
                            anyhow!(
                                "Blobfs inode inline extent start too large: {}",
                                inode.inline_extent.start()
                            )
                        },
                    )?;
                // Compute absolute offset where merkle and data are stored.
                let blob_data_offset = data_offset.checked_add(extent_offset)
                    .ok_or_else(|| anyhow!("Blobfs data + inode inline extent start overflowed: data_offset={} + extent_offset={}", data_offset, extent_offset))?;

                let (data_start, merkle_tree_size) = match blobfs_version {
                    BlobFsVersion::Version8 => {
                        // Data begins with merkle tree. Skip passed merkle tree.
                        let merkle_tree_block_count = merkle_tree_block_count(&inode);
                        let merkle_tree_size = merkle_tree_block_count.checked_mul(BLOBFS_BLOCK_SIZE)
                            .ok_or_else(|| anyhow!("Blobfs merkle tree block count overflows when multiplied by block size: block_count={}, block_size={}", merkle_tree_block_count, BLOBFS_BLOCK_SIZE))?;
                        let data_start = blob_data_offset.checked_add(merkle_tree_size).ok_or_else(|| anyhow!("Malformed blobfs archive offset: data_offset={} + merkle_tree_size={}", blob_data_offset, merkle_tree_size))?;
                        (data_start, merkle_tree_size)
                    }
                    BlobFsVersion::Version9 => {
                        // Merkle tree appears at the end of data; no need to add more to offset.
                        let data_start = blob_data_offset;
                        let merkle_tree_size = merkle_tree_byte_size(&inode, true);
                        (data_start, merkle_tree_size)
                    }
                };

                let block_length = inode.inline_extent.length().checked_mul(BLOBFS_BLOCK_SIZE)
                    .ok_or_else(|| anyhow!("Blobfs inode inline extent length overflows when multiplied by block size: block_count={}, block_size={}", inode.inline_extent.length(), BLOBFS_BLOCK_SIZE))?;
                let data_length = block_length.checked_sub(merkle_tree_size)
                    .ok_or_else(|| anyhow!("Blobfs content + merkle tree size is less than merkle tree size: total_size={}, merkle_tree_size={}", block_length, merkle_tree_size))?;

                metadata.insert(merkle, BlobMetadata { prelude, inode, data_start, data_length });
            }
        }
        Ok(metadata)
    }
}

/// Bespoke reader interface for blobfs that supports reading individual blobs
/// named by hex-string paths, and iterating over all valid blob merkle root
/// paths.
pub struct BlobFsReader<TCRS: TryClone + Read + Seek> {
    reader_seeker: TCRS,
    tmp_dir: Arc<TemporaryDirectory>,
    metadata: Arc<HashMap<PathBuf, BlobMetadata>>,
}

impl<TCRS: TryClone + Read + Seek> TryClone for BlobFsReader<TCRS> {
    fn try_clone(&self) -> Result<Self> {
        Ok(Self {
            reader_seeker: self.reader_seeker.try_clone().context("blobfs reader")?,
            tmp_dir: self.tmp_dir.clone(),
            metadata: self.metadata.clone(),
        })
    }
}

impl<TCRS: 'static + TryClone + Read + Seek> BlobFsReader<TCRS> {
    /// Open a blob as a `std::io::Read + std::io::Seek`.
    pub fn open<P: AsRef<Path>>(&mut self, blob_path: P) -> Result<Box<dyn ReadSeek>> {
        let metadata = self
            .metadata
            .get(blob_path.as_ref())
            .ok_or_else(|| anyhow!("Blobfs blob not found: {:?}", blob_path.as_ref()))?;
        if metadata.prelude.is_chunk_compressed() {
            // TODO(fxbug.dev/102061): Eliminate in-memory copy of compressed blob and/or leverage
            // memory mapped files.
            let mut buffer = vec![0u8; metadata.data_length as usize];
            self.reader_seeker.seek(SeekFrom::Start(metadata.data_start))?;
            self.reader_seeker.read_exact(&mut buffer)?;
            let mut decompressed =
                zstd::chunked_decompress(&buffer, metadata.inode.blob_size.try_into().unwrap());

            if decompressed.len() == 0 {
                return Err(anyhow!(
                    "Failed to decompress chunk-compressed blob: {:?}",
                    blob_path.as_ref()
                ));
            }

            decompressed.truncate(metadata.inode.blob_size as usize);
            let blob_tmp_path = self.tmp_dir.as_path().join(blob_path);
            std::fs::write(&blob_tmp_path, decompressed)?;

            File::open(&blob_tmp_path)
                .map(BufReader::new)
                .map(|reader| Box::new(reader) as Box<dyn ReadSeek>)
                .map_err(|err| {
                    anyhow!("Failed to open decompressed blob file {:?}: {}", &blob_tmp_path, err)
                })
        } else {
            let reader_seeker =
                self.reader_seeker.try_clone().context("opening file from blobfs archive")?;
            WrappedReaderSeeker::builder()
                .reader_seeker(reader_seeker)
                .offset(metadata.data_start)
                .length(metadata.inode.blob_size)
                .build()
                .map(|reader| Box::new(reader) as Box<dyn ReadSeek>)
        }
    }

    /// Construct an iterator of all known paths stored in the underlying blobfs
    /// archive.
    pub fn blob_paths<'a>(&'a self) -> Box<dyn Iterator<Item = &'a PathBuf> + 'a> {
        Box::new(self.metadata.keys())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::{
            calculate_hash_list_size, merkle_tree_block_count, round_up, BlobFsError, BlobFsHeader,
            BlobFsReaderBuilder, Extent, Inode, NodePrelude, BLOBFS_FLAG_ALLOCATED, BLOBFS_MAGIC_0,
            BLOBFS_MAGIC_1, BLOBFS_VERSION_8, BLOBFS_VERSION_9,
        },
        crate::{
            fs::TemporaryDirectory,
            io::{TryClonableBufReaderFile, TryClone},
        },
        std::{
            fs::{write, File},
            io::{BufReader, Cursor, Read, Seek},
            sync::Arc,
        },
        tempfile::tempdir,
    };

    fn fake_blobfs_header() -> BlobFsHeader {
        BlobFsHeader {
            magic_0: BLOBFS_MAGIC_0,
            magic_1: BLOBFS_MAGIC_1,
            version: BLOBFS_VERSION_9,
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

    fn file_and_buffer_test_from_buffer(
        buffer: Vec<u8>,
        file_test: Box<dyn Fn(BlobFsReaderBuilder<TryClonableBufReaderFile>)>,
        buffer_test: Box<dyn Fn(BlobFsReaderBuilder<Cursor<Vec<u8>>>)>,
    ) {
        let dir: Arc<TemporaryDirectory> = Arc::new(tempdir().unwrap().into());
        let blobfs_path = dir.as_path().join("blob.blk");
        write(&blobfs_path, buffer.as_slice()).unwrap();
        let reader: TryClonableBufReaderFile =
            BufReader::new(File::open(&blobfs_path).unwrap()).into();
        let builder =
            BlobFsReaderBuilder::new().archive(reader).unwrap().tmp_dir(dir.clone()).unwrap();
        file_test(builder);

        let builder =
            BlobFsReaderBuilder::new().archive(Cursor::new(buffer)).unwrap().tmp_dir(dir).unwrap();
        buffer_test(builder);
    }

    fn blobfs_reader_builder_build_err<TCRS: TryClone + Read + Seek + Send + Sync>(
        builder: BlobFsReaderBuilder<TCRS>,
    ) {
        let result = builder.build();
        assert!(result.is_err());
    }

    fn blobfs_reader_builder_build_err_eq<TCRS: TryClone + Read + Seek + Send + Sync>(
        err: BlobFsError,
    ) -> Box<dyn Fn(BlobFsReaderBuilder<TCRS>)> {
        Box::new(move |builder: BlobFsReaderBuilder<TCRS>| {
            let result = builder.build();
            assert!(result.is_err());
            assert_eq!(err, result.err().unwrap().downcast::<BlobFsError>().unwrap());
        })
    }

    fn blobfs_reader_builder_build_ok_num_blobs<
        TCRS: 'static + TryClone + Read + Seek + Send + Sync,
    >(
        num_blobs: usize,
    ) -> Box<dyn Fn(BlobFsReaderBuilder<TCRS>)> {
        Box::new(move |builder: BlobFsReaderBuilder<TCRS>| {
            let reader = builder.build().unwrap();
            assert_eq!(num_blobs, reader.blob_paths().count());
        })
    }

    #[fuchsia::test]
    fn test_blobfs_empty_invalid() {
        let blobfs_bytes = vec![0u8];
        file_and_buffer_test_from_buffer(
            blobfs_bytes,
            Box::new(blobfs_reader_builder_build_err::<TryClonableBufReaderFile>),
            Box::new(blobfs_reader_builder_build_err::<Cursor<Vec<u8>>>),
        );
    }

    #[fuchsia::test]
    fn test_round_up() {
        assert_eq!(round_up(10, 64), 64);
        assert_eq!(round_up(100, 64), 128);
        assert_eq!(round_up(128, 128), 128);
        assert_eq!(round_up(129, 128), 256);
        assert_eq!(round_up(0, 128), 0);
        assert_eq!(round_up(5, 1), 5);
    }

    #[fuchsia::test]
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

    #[fuchsia::test]
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

    #[fuchsia::test]
    fn test_blobfs_reader_bad_magic_value() {
        let mut header = fake_blobfs_header();
        header.magic_0 = 0;
        let blobfs_bytes = bincode::serialize(&header).unwrap();

        file_and_buffer_test_from_buffer(
            blobfs_bytes,
            blobfs_reader_builder_build_err_eq::<TryClonableBufReaderFile>(
                BlobFsError::InvalidHeaderMagic,
            ),
            blobfs_reader_builder_build_err_eq::<Cursor<Vec<u8>>>(BlobFsError::InvalidHeaderMagic),
        );
    }

    #[fuchsia::test]
    fn test_blobfs_reader_bad_version_value() {
        let mut header = fake_blobfs_header();
        header.version = 500;
        let blobfs_bytes = bincode::serialize(&header).unwrap();
        file_and_buffer_test_from_buffer(
            blobfs_bytes,
            blobfs_reader_builder_build_err_eq::<TryClonableBufReaderFile>(
                BlobFsError::UnsupportedVersion,
            ),
            blobfs_reader_builder_build_err_eq::<Cursor<Vec<u8>>>(BlobFsError::UnsupportedVersion),
        );
    }

    #[fuchsia::test]
    fn test_blobfs_no_allocations() {
        let header = fake_blobfs_header();
        let mut blobfs_bytes = bincode::serialize(&header).unwrap();
        let mut empty_data = vec![0u8; 8192 * 20];
        blobfs_bytes.append(&mut empty_data);
        file_and_buffer_test_from_buffer(
            blobfs_bytes,
            blobfs_reader_builder_build_ok_num_blobs::<TryClonableBufReaderFile>(0),
            blobfs_reader_builder_build_ok_num_blobs::<Cursor<Vec<u8>>>(0),
        );
    }

    #[fuchsia::test]
    fn test_blobfs_allocations() {
        let block_size: usize = 8192;
        let mut header = fake_blobfs_header();
        header.inode_count = 1;
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
        file_and_buffer_test_from_buffer(
            blobfs_bytes,
            blobfs_reader_builder_build_ok_num_blobs::<TryClonableBufReaderFile>(1),
            blobfs_reader_builder_build_ok_num_blobs::<Cursor<Vec<u8>>>(1),
        );
    }

    #[fuchsia::test]
    fn test_blobfs_old_compat_version() {
        let mut header = fake_blobfs_header();
        header.version = BLOBFS_VERSION_8;
        let mut blobfs_bytes = bincode::serialize(&header).unwrap();
        let mut empty_data = vec![0u8; 8192 * 20];
        blobfs_bytes.append(&mut empty_data);
        file_and_buffer_test_from_buffer(
            blobfs_bytes,
            blobfs_reader_builder_build_ok_num_blobs::<TryClonableBufReaderFile>(0),
            blobfs_reader_builder_build_ok_num_blobs::<Cursor<Vec<u8>>>(0),
        );
    }
}
