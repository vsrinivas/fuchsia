/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2010 Zheng Liu <lz@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::readers::{Reader, ReaderError},
    byteorder::LittleEndian,
    std::{collections::HashMap, fmt, mem::size_of, str, sync::Arc},
    thiserror::Error,
    zerocopy::{FromBytes, LayoutVerified, Unaligned, U16, U32, U64},
};

// Block Group 0 Padding
pub const FIRST_BG_PADDING: usize = 1024;
// INode number of root directory '/'.
pub const ROOT_INODE_NUM: u32 = 2;
// EXT 2/3/4 magic number.
pub const SB_MAGIC: u16 = 0xEF53;
// Extent Header magic number.
pub const EH_MAGIC: u16 = 0xF30A;
// Any smaller would not even fit the first copy of the ext4 Super Block.
pub const MIN_EXT4_SIZE: usize = FIRST_BG_PADDING + size_of::<SuperBlock>();

type LEU16 = U16<LittleEndian>;
type LEU32 = U32<LittleEndian>;
type LEU64 = U64<LittleEndian>;

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct ExtentHeader {
    /// Magic number: 0xF30A
    pub eh_magic: LEU16,
    /// Number of valid entries.
    pub eh_ecount: LEU16,
    /// Entry capacity.
    pub eh_max: LEU16,
    /// Depth distance this node is from its leaves.
    /// `0` here means it is a leaf node.
    pub eh_depth: LEU16,
    /// Generation of extent tree.
    pub eh_gen: LEU32,
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(ExtentHeader, [u8; 12]);

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct ExtentIndex {
    /// Indexes logical blocks.
    pub ei_blk: LEU32,
    /// Points to the physical block of the next level.
    pub ei_leaf_lo: LEU32,
    /// High 16 bits of physical block.
    pub ei_leaf_hi: LEU16,
    pub ei_unused: LEU16,
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(ExtentIndex, [u8; 12]);

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct Extent {
    /// First logical block.
    pub e_blk: LEU32,
    /// Number of blocks.
    pub e_len: LEU16,
    /// High 16 bits of physical block.
    pub e_start_hi: LEU16,
    /// Low 32 bits of physical block.
    pub e_start_lo: LEU32,
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(Extent, [u8; 12]);

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct DirEntry2 {
    /// INode number of entry
    pub e2d_ino: LEU32,
    /// Length of this record.
    pub e2d_reclen: LEU16,
    /// Length of string in `e2d_name`.
    pub e2d_namlen: u8,
    /// File type of this entry.
    pub e2d_type: u8,

    // TODO(vfcc): Actual size varies by e2d_reclen.
    // For now, we will read the max length and ignore the trailing bytes.
    /// Name of the entry.
    pub e2d_name: [u8; 255],
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(DirEntry2, [u8; 263]);

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct SuperBlock {
    /// INode count.
    pub e2fs_icount: LEU32,
    /// Block count.
    pub e2fs_bcount: LEU32,
    /// Reserved blocks count.
    pub e2fs_rbcount: LEU32,
    /// Free blocks count.
    pub e2fs_fbcount: LEU32,
    /// Free INodes count.
    pub e2fs_ficount: LEU32,
    /// First data block.
    pub e2fs_first_dblock: LEU32,
    /// Block Size = 2^(e2fs_log_bsize+10).
    pub e2fs_log_bsize: LEU32,
    /// Fragment size.
    pub e2fs_log_fsize: LEU32,
    /// Blocks per group.
    pub e2fs_bpg: LEU32,
    /// Fragments per group.
    pub e2fs_fpg: LEU32,
    /// INodes per group.
    pub e2fs_ipg: LEU32,
    /// Mount time.
    pub e2fs_mtime: LEU32,
    /// Write time.
    pub e2fs_wtime: LEU32,
    /// Mount count.
    pub e2fs_mnt_count: LEU16,
    /// Max mount count.
    pub e2fs_max_mnt_count: LEU16,
    /// Magic number: 0xEF53
    pub e2fs_magic: LEU16,
    /// Filesystem state.
    pub e2fs_state: LEU16,
    /// Behavior on errors.
    pub e2fs_beh: LEU16,
    /// Minor revision level.
    pub e2fs_minrev: LEU16,
    /// Time of last filesystem check.
    pub e2fs_lastfsck: LEU32,
    /// Max time between filesystem checks.
    pub e2fs_fsckintv: LEU32,
    /// Creator OS.
    pub e2fs_creator: LEU32,
    /// Revision level.
    pub e2fs_rev: LEU32,
    /// Default UID for reserved blocks.
    pub e2fs_ruid: LEU16,
    /// Default GID for reserved blocks.
    pub e2fs_rgid: LEU16,
    /// First non-reserved inode.
    pub e2fs_first_ino: LEU32,
    /// Size of INode structure.
    pub e2fs_inode_size: LEU16,
    /// Block group number of this super block.
    pub e2fs_block_group_nr: LEU16,
    /// Compatible feature set.
    pub e2fs_features_compat: LEU32,
    /// Incompatible feature set.
    pub e2fs_features_incompat: LEU32,
    /// RO-compatible feature set.
    pub e2fs_features_rocompat: LEU32,
    /// 128-bit uuid for volume.
    pub e2fs_uuid: [u8; 16],
    /// Volume name.
    pub e2fs_vname: [u8; 16],
    /// Name as mounted.
    pub e2fs_fsmnt: [u8; 64],
    /// Compression algorithm.
    pub e2fs_algo: LEU32,
    /// # of blocks for old prealloc.
    pub e2fs_prealloc: u8,
    /// # of blocks for old prealloc dirs.
    pub e2fs_dir_prealloc: u8,
    /// # of reserved gd blocks for resize.
    pub e2fs_reserved_ngdb: LEU16,
    /// UUID of journal super block.
    pub e3fs_journal_uuid: [u8; 16],
    /// INode number of journal file.
    pub e3fs_journal_inum: LEU32,
    /// Device number of journal file.
    pub e3fs_journal_dev: LEU32,
    /// Start of list of inodes to delete.
    pub e3fs_last_orphan: LEU32,
    /// HTREE hash seed.
    pub e3fs_hash_seed: [LEU32; 4],
    /// Default hash version to use.
    pub e3fs_def_hash_version: u8,
    /// Journal backup type.
    pub e3fs_jnl_backup_type: u8,
    /// size of group descriptor.
    pub e3fs_desc_size: LEU16,
    /// Default mount options.
    pub e3fs_default_mount_opts: LEU32,
    /// First metablock block group.
    pub e3fs_first_meta_bg: LEU32,
    /// When the filesystem was created.
    pub e3fs_mkfs_time: LEU32,
    /// Backup of the journal INode.
    pub e3fs_jnl_blks: [LEU32; 17],
    /// High bits of block count.
    pub e4fs_bcount_hi: LEU32,
    /// High bits of reserved blocks count.
    pub e4fs_rbcount_hi: LEU32,
    /// High bits of free blocks count.
    pub e4fs_fbcount_hi: LEU32,
    /// All inodes have some bytes.
    pub e4fs_min_extra_isize: LEU16,
    /// Inodes must reserve some bytes.
    pub e4fs_want_extra_isize: LEU16,
    /// Miscellaneous flags.
    pub e4fs_flags: LEU32,
    /// RAID stride.
    pub e4fs_raid_stride: LEU16,
    /// Seconds to wait in MMP checking.
    pub e4fs_mmpintv: LEU16,
    /// Block for multi-mount protection.
    pub e4fs_mmpblk: LEU64,
    /// Blocks on data disks (N * stride).
    pub e4fs_raid_stripe_wid: LEU32,
    /// FLEX_BG group size.
    pub e4fs_log_gpf: u8,
    /// Metadata checksum algorithm used.
    pub e4fs_chksum_type: u8,
    /// Versioning level for encryption.
    pub e4fs_encrypt: u8,
    pub e4fs_reserved_pad: u8,
    /// Number of lifetime kilobytes.
    pub e4fs_kbytes_written: LEU64,
    /// INode number of active snapshot.
    pub e4fs_snapinum: LEU32,
    /// Sequential ID of active snapshot.
    pub e4fs_snapid: LEU32,
    /// Reserved blocks for active snapshot.
    pub e4fs_snaprbcount: LEU64,
    /// INode number for on-disk snapshot.
    pub e4fs_snaplist: LEU32,
    /// Number of filesystem errors.
    pub e4fs_errcount: LEU32,
    /// First time an error happened.
    pub e4fs_first_errtime: LEU32,
    /// INode involved in first error.
    pub e4fs_first_errino: LEU32,
    /// Block involved of first error.
    pub e4fs_first_errblk: LEU64,
    /// Function where error happened.
    pub e4fs_first_errfunc: [u8; 32],
    /// Line number where error happened.
    pub e4fs_first_errline: LEU32,
    /// Most recent time of an error.
    pub e4fs_last_errtime: LEU32,
    /// INode involved in last error.
    pub e4fs_last_errino: LEU32,
    /// Line number where error happened.
    pub e4fs_last_errline: LEU32,
    /// Block involved of last error.
    pub e4fs_last_errblk: LEU64,
    /// Function where error happened.
    pub e4fs_last_errfunc: [u8; 32],
    /// Mount options.
    pub e4fs_mount_opts: [u8; 64],
    /// INode for tracking user quota.
    pub e4fs_usrquota_inum: LEU32,
    /// INode for tracking group quota.
    pub e4fs_grpquota_inum: LEU32,
    /// Overhead blocks/clusters.
    pub e4fs_overhead_clusters: LEU32,
    /// Groups with sparse_super2 SBs.
    pub e4fs_backup_bgs: [LEU32; 2],
    /// Encryption algorithms in use.
    pub e4fs_encrypt_algos: [u8; 4],
    /// Salt used for string2key.
    pub e4fs_encrypt_pw_salt: [u8; 16],
    /// Location of the lost+found inode.
    pub e4fs_lpf_ino: LEU32,
    /// INode for tracking project quota.
    pub e4fs_proj_quota_inum: LEU32,
    /// Checksum seed.
    pub e4fs_chksum_seed: LEU32,
    /// Padding to the end of the block.
    pub e4fs_reserved: [LEU32; 98],
    /// Super block checksum.
    pub e4fs_sbchksum: LEU32,
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(SuperBlock, [u8; 1024]);

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct BlockGroupDesc32 {
    /// Blocks bitmap block.
    pub ext2bgd_b_bitmap: LEU32,
    /// INodes bitmap block.
    pub ext2bgd_i_bitmap: LEU32,
    /// INodes table block.
    pub ext2bgd_i_tables: LEU32,
    /// # Free blocks.
    pub ext2bgd_nbfree: LEU16,
    /// # Free INodes.
    pub ext2bgd_nifree: LEU16,
    /// # Directories.
    pub ext2bgd_ndirs: LEU16,
    /// Block group flags.
    pub ext4bgd_flags: LEU16,
    /// Snapshot exclusion bitmap location.
    pub ext4bgd_x_bitmap: LEU32,
    /// Block bitmap checksum.
    pub ext4bgd_b_bmap_csum: LEU16,
    /// INode bitmap checksum.
    pub ext4bgd_i_bmap_csum: LEU16,
    /// Unused INode count.
    pub ext4bgd_i_unused: LEU16,
    /// Group descriptor checksum.
    pub ext4bgd_csum: LEU16,
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(BlockGroupDesc32, [u8; 32]);

// TODO(vfcc): There are more fields in BlockGroupDesc if the filesystem is 64bit.
// Uncomment this when we add support.
// #[derive(FromBytes, Unaligned)]
// #[repr(C)]
// pub struct BlockGroupDesc64 {
//     pub base: BlockGroupDesc32,
//     pub ext4bgd_b_bitmap_hi: LEU32,
//     pub ext4bgd_i_bitmap_hi: LEU32,
//     pub ext4bgd_i_tables_hi: LEU32,
//     pub ext4bgd_nbfree_hi: LEU16,
//     pub ext4bgd_nifree_hi: LEU16,
//     pub ext4bgd_ndirs_hi: LEU16,
//     pub ext4bgd_i_unused_hi: LEU16,
//     pub ext4bgd_x_bitmap_hi: LEU32,
//     pub ext4bgd_b_bmap_csum_hi: LEU16,
//     pub ext4bgd_i_bmap_csum_hi: LEU16,
//     pub ext4bgd_reserved: LEU32,
// }
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
// assert_eq_size!(BlockGroupDesc64, [u8; 64]);

#[derive(FromBytes, Unaligned)]
#[repr(C)]
pub struct INode {
    /// Access permission flags.
    pub e2di_mode: LEU16,
    /// Owner UID.
    pub e2di_uid: LEU16,
    /// Size (in bytes).
    pub e2di_size: LEU32,
    /// Access time.
    pub e2di_atime: LEU32,
    /// Change time.
    pub e2di_ctime: LEU32,
    /// Modification time.
    pub e2di_mtime: LEU32,
    /// Deletion time.
    pub e2di_dtime: LEU32,
    /// Owner GID.
    pub e2di_gid: LEU16,
    /// File link count.
    pub e2di_nlink: LEU16,
    /// Block count.
    pub e2di_nblock: LEU32,
    /// Status flags.
    pub e2di_flags: LEU32,
    /// INode version.
    pub e2di_version: [u8; 4],
    /// Extent tree.
    pub e2di_blocks: [u8; 60],
    /// Generation.
    pub e2di_gen: LEU32,
    /// EA block.
    pub e2di_facl: LEU32,
    /// High bits for file size.
    pub e2di_size_high: LEU32,
    /// Fragment address (obsolete).
    pub e2di_faddr: LEU32,
    /// High bits for block count.
    pub e2di_nblock_high: LEU16,
    /// High bits for EA block.
    pub e2di_facl_high: LEU16,
    /// High bits for Owner UID.
    pub e2di_uid_high: LEU16,
    /// High bits for Owner GID.
    pub e2di_gid_high: LEU16,
    /// High bits for INode checksum.
    pub e2di_chksum_lo: LEU16,
    pub e2di_lx_reserved: LEU16,
}
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
assert_eq_size!(INode, [u8; 128]);

// TODO(vfcc): There are more fields in the INode table, but they depend on
// e2di_extra_isize.
// Uncomment this if, at a later date, we add support for these fields.
// pub struct INodeExtra {
//     pub base: INode,
//     pub e2di_extra_isize: LEU16,
//     pub e2di_chksum_hi: LEU16,
//     pub e2di_ctime_extra: LEU32,
//     pub e2di_mtime_extra: LEU32,
//     pub e2di_atime_extra: LEU32,
//     pub e2di_crtime: LEU32,
//     pub e2di_crtime_extra: LEU32,
//     pub e2di_version_hi: LEU32,
//     pub e2di_projid: LEU32,
// }
// Make sure our struct's size matches the Ext4 spec.
// https://ext4.wiki.kernel.org/index.php/Ext4_Disk_Layout
// assert_eq_size!(INodeExtra, [u8; 160]);

#[derive(Debug, PartialEq)]
pub enum InvalidAddressErrorType {
    Lower,
    Upper,
}

impl fmt::Display for InvalidAddressErrorType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match *self {
            InvalidAddressErrorType::Lower => write!(f, "lower"),
            InvalidAddressErrorType::Upper => write!(f, "upper"),
        }
    }
}

#[derive(Error, Debug, PartialEq)]
pub enum ParsingError {
    #[error("Unable to parse Super Block at 0x{:X}", _0)]
    InvalidSuperBlock(usize),
    #[error("Invalid Super Block magic number {} should be 0xEF53", _0)]
    InvalidSuperBlockMagic(u16),
    #[error("Block number {} out of bounds.", _0)]
    BlockNumberOutOfBounds(u64),
    #[error("SuperBlock e2fs_log_bsize value invalid: {}", _0)]
    BlockSizeInvalid(u32),

    #[error("Unable to parse Block Group Description at 0x{:X}", _0)]
    InvalidBlockGroupDesc(usize),
    #[error("Unable to parse INode {}", _0)]
    InvalidInode(u32),

    // TODO(vfcc): A followup change will add the ability to include an address here.
    #[error("Unable to parse ExtentHeader from INode")]
    InvalidExtentHeader,
    #[error("Invalid Extent Header magic number {} should be 0xF30A", _0)]
    InvalidExtentHeaderMagic(u16),
    #[error("Unable to parse Extent at 0x{:X}", _0)]
    InvalidExtent(usize),
    #[error("Extent has more data {} than expected {}", _0, _1)]
    ExtentUnexpectedLength(usize, usize),

    #[error("Invalid Directory Entry at 0x{:X}", _0)]
    InvalidDirEntry2(usize),
    #[error("Directory Entry has invalid string in name field: {:?}", _0)]
    DirEntry2NonUtf8(Vec<u8>),
    #[error("Requested path contains invalid string")]
    InvalidInputPath,
    #[error("Non-existent path: {}", _0)]
    PathNotFound(String),
    #[error("Entry Type {} unknown", _0)]
    BadEntryType(u8),

    /// Feature Incompatible flags
    #[error("Incompatible feature flags (feature_incompat): 0x{:X}", _0)]
    BannedFeatureIncompat(u32),
    #[error("Required feature flags (feature_incompat): 0x{:X}", _0)]
    RequiredFeatureIncompat(u32),

    /// Message including what ext filesystem feature was found that we do not support
    #[error("{}", _0)]
    Incompatible(String),
    #[error("Bad file at {}", _0)]
    BadFile(String),
    #[error("Bad directory at {}", _0)]
    BadDirectory(String),

    #[error("Attempted to access at 0x{:X} when the {} bound is 0x{:X}", _1, _0, _2)]
    InvalidAddress(InvalidAddressErrorType, usize, usize),

    #[error("Reader failed to read at 0x{:X}", _0)]
    SourceReadError(usize),
}

impl From<ReaderError> for ParsingError {
    fn from(err: ReaderError) -> ParsingError {
        match err {
            ReaderError::Read(addr) => ParsingError::SourceReadError(addr),
            ReaderError::OutOfBounds(addr, max) => {
                ParsingError::InvalidAddress(InvalidAddressErrorType::Upper, addr, max)
            }
        }
    }
}

/// Directory Entry types.
#[derive(PartialEq)]
#[repr(u8)]
pub enum EntryType {
    Unknown = 0x0,
    RegularFile = 0x1,
    Directory = 0x2,
    CharacterDevice = 0x3,
    BlockDevice = 0x4,
    FIFO = 0x5,
    Socket = 0x6,
    SymLink = 0x7,
}

impl EntryType {
    pub fn from_u8(value: u8) -> Result<EntryType, ParsingError> {
        match value {
            0x0 => Ok(EntryType::Unknown),
            0x1 => Ok(EntryType::RegularFile),
            0x2 => Ok(EntryType::Directory),
            0x3 => Ok(EntryType::CharacterDevice),
            0x4 => Ok(EntryType::BlockDevice),
            0x5 => Ok(EntryType::FIFO),
            0x6 => Ok(EntryType::Socket),
            0x7 => Ok(EntryType::SymLink),
            _ => Err(ParsingError::BadEntryType(value)),
        }
    }
}

/// Feature Incompatible flags.
///
/// Stored in SuperBlock::e2fs_features_incompat.
///
/// All flags listed by a given filesystem must be supported by us in order for us to attempt
/// mounting it.
///
/// With our limited support at the time, there are also flags that we require to exist, like
/// `EntryHasFileType`.
#[derive(PartialEq)]
#[repr(u32)]
pub enum FeatureIncompat {
    Compression = 0x1,
    /// Currently required flag.
    EntryHasFileType = 0x2,

    // TODO(vfcc): We will permit journaling because our initial use will not have entries in the
    // journal. Will need to add proper support in the future.
    /// We do not support journaling, but assuming an empty journal, we can still read.
    ///
    /// Run `fsck` in Linux to repair the filesystem first before attempting to mount a journaled
    /// ext4 image.
    HasJournal = 0x4,
    JournalSeparate = 0x8,
    MetaBlockGroups = 0x10,
    /// Required flag. Lack of this flag means the filesystem is not ext4, and we are not
    /// backward compatible.
    Extents = 0x40,
    Is64Bit = 0x80,
    MultiMountProtection = 0x100,

    // TODO(vfcc): Should be relatively trivial to support.
    /// No explicit support, we will permit the flag as it works for our needs.
    FlexibleBlockGroups = 0x200,
    ExtendedAttributeINodes = 0x400,
    ExtendedDirectoryEntry = 0x1000,
    /// We do not calculate checksums, so this is permitted but not actionable.
    MetadataChecksum = 0x2000,
    LargeDirectory = 0x4000,
    SmallFilesInINode = 0x8000,
    EncryptedINodes = 0x10000,
}

/// Required "feature incompatible" flags.
pub const REQUIRED_FEATURE_INCOMPAT: u32 =
    FeatureIncompat::Extents as u32 | FeatureIncompat::EntryHasFileType as u32;

/// Banned "feature incompatible" flags.
pub const BANNED_FEATURE_INCOMPAT: u32 = FeatureIncompat::Compression as u32 |
    // TODO(vfcc): Possibly trivial to support.
    FeatureIncompat::Is64Bit as u32 |
    FeatureIncompat::MultiMountProtection as u32 |
    FeatureIncompat::ExtendedAttributeINodes as u32 |
    FeatureIncompat::ExtendedDirectoryEntry as u32 |
    // TODO(vfcc): This should be relatively trivial to support.
    FeatureIncompat::SmallFilesInINode as u32 |
    FeatureIncompat::EncryptedINodes as u32;

/// All functions to help parse data into respective structs.
pub trait ParseToStruct: FromBytes + Unaligned + Sized {
    fn parse_offset(
        reader: Arc<dyn Reader>,
        offset: usize,
        error_type: ParsingError,
    ) -> Result<Arc<Self>, ParsingError> {
        let data = Self::read_from_offset(reader, offset)?;
        Self::to_struct_arc(data, error_type)
    }

    fn read_from_offset(reader: Arc<dyn Reader>, offset: usize) -> Result<Box<[u8]>, ParsingError> {
        if offset < FIRST_BG_PADDING {
            return Err(ParsingError::InvalidAddress(
                InvalidAddressErrorType::Lower,
                offset,
                FIRST_BG_PADDING,
            ));
        }
        let mut data = vec![0u8; size_of::<Self>()];
        reader.read(offset, data.as_mut_slice())?;
        Ok(data.into_boxed_slice())
    }

    /// Transmutes from `Box<[u8]>` to `Arc<Self>`.
    ///
    /// `data` is consumed by this operation.
    ///
    /// `Self` is the ext4 struct that represents the given `data`.
    fn to_struct_arc(data: Box<[u8]>, error: ParsingError) -> Result<Arc<Self>, ParsingError> {
        Self::validate(&data, error)?;
        let ptr = Box::into_raw(data);
        unsafe { Ok(Arc::from(Box::from_raw(ptr as *mut Self))) }
    }

    /// Casts the &[u8] data to &Self.
    ///
    /// `Self` is the ext4 struct that represents the given `data`.
    fn to_struct_ref(data: &[u8], error_type: ParsingError) -> Result<&Self, ParsingError> {
        LayoutVerified::<&[u8], Self>::new(data).map(|res| res.into_ref()).ok_or(error_type)
    }

    fn validate(data: &[u8], error_type: ParsingError) -> Result<(), ParsingError> {
        Self::to_struct_ref(data, error_type).map(|_| ())
    }
}

/// Apply to all EXT4 structs as seen above.
impl<T: FromBytes + Unaligned> ParseToStruct for T {}

impl SuperBlock {
    /// Parse the Super Block at its default location.
    pub fn parse(reader: Arc<dyn Reader>) -> Result<Arc<SuperBlock>, ParsingError> {
        // Super Block in Block Group 0 is at offset 1024.
        // Assuming there is no corruption, there is no need to read any other
        // copy of the Super Block.
        let data = SuperBlock::read_from_offset(reader, FIRST_BG_PADDING)?;
        let sb =
            SuperBlock::to_struct_arc(data, ParsingError::InvalidSuperBlock(FIRST_BG_PADDING))?;
        sb.check_magic()?;
        sb.feature_check()?;
        Ok(sb)
    }

    pub fn check_magic(&self) -> Result<(), ParsingError> {
        if self.e2fs_magic.get() == SB_MAGIC {
            Ok(())
        } else {
            Err(ParsingError::InvalidSuperBlockMagic(self.e2fs_magic.get()))
        }
    }

    /// Reported block size.
    ///
    /// Per spec, the only valid block sizes are 1KiB, 2KiB, 4KiB, and 64KiB. We will only
    /// permit these values.
    ///
    /// Returning as `usize` for ease of use in calculations.
    pub fn block_size(&self) -> Result<usize, ParsingError> {
        let bs = 2usize
            .checked_pow(self.e2fs_log_bsize.get() + 10)
            .ok_or(ParsingError::BlockSizeInvalid(self.e2fs_log_bsize.get()))?;
        if bs == 1024 || bs == 2048 || bs == 4096 || bs == 65536 {
            Ok(bs)
        } else {
            Err(ParsingError::BlockSizeInvalid(self.e2fs_log_bsize.get()))
        }
    }

    fn feature_check(&self) -> Result<(), ParsingError> {
        let banned = self.e2fs_features_incompat.get() & BANNED_FEATURE_INCOMPAT;
        if banned > 0 {
            return Err(ParsingError::BannedFeatureIncompat(banned));
        }
        let required = self.e2fs_features_incompat.get() & REQUIRED_FEATURE_INCOMPAT;
        if required != REQUIRED_FEATURE_INCOMPAT {
            return Err(ParsingError::RequiredFeatureIncompat(
                required ^ REQUIRED_FEATURE_INCOMPAT,
            ));
        }
        Ok(())
    }
}

impl INode {
    /// INode contains the root of its Extent tree within `e2di_blocks`.
    /// Read `e2di_blocks` and return the root Extent Header.
    pub fn root_extent_header(&self) -> Result<&ExtentHeader, ParsingError> {
        let eh = ExtentHeader::to_struct_ref(
            // Bounds here are known and static on a field that is defined to be much larger
            // than an `ExtentHeader`.
            &(self.e2di_blocks)[0..size_of::<ExtentHeader>()],
            ParsingError::InvalidExtentHeader,
        )?;
        eh.check_magic()?;
        Ok(eh)
    }

    /// Size of the file/directory/entry represented by this INode.
    pub fn size(&self) -> usize {
        (self.e2di_size_high.get() as usize) << 32 | self.e2di_size.get() as usize
    }
}

impl ExtentHeader {
    pub fn check_magic(&self) -> Result<(), ParsingError> {
        if self.eh_magic.get() == EH_MAGIC {
            Ok(())
        } else {
            Err(ParsingError::InvalidExtentHeaderMagic(self.eh_magic.get()))
        }
    }
}

impl DirEntry2 {
    /// Name of the file/directory/entry as a string.
    pub fn name(&self) -> Result<&str, ParsingError> {
        str::from_utf8(&self.e2d_name[0..self.e2d_namlen as usize]).map_err(|_| {
            ParsingError::DirEntry2NonUtf8(self.e2d_name[0..self.e2d_namlen as usize].to_vec())
        })
    }

    /// Generate a hash table of the given directory entries.
    ///
    /// Key: name of entry
    /// Value: DirEntry2 struct
    pub fn as_hash_map(
        entries: Vec<Arc<DirEntry2>>,
    ) -> Result<HashMap<String, Arc<DirEntry2>>, ParsingError> {
        let mut entry_map: HashMap<String, Arc<DirEntry2>> = HashMap::with_capacity(entries.len());

        for entry in entries {
            entry_map.insert(entry.name()?.to_string(), entry);
        }
        Ok(entry_map)
    }
}

impl Extent {
    /// Block number that this Extent points to.
    pub fn target_block_num(&self) -> u64 {
        (self.e_start_hi.get() as u64) << 32 | self.e_start_lo.get() as u64
    }
}

impl ExtentIndex {
    /// Block number that this ExtentIndex points to.
    pub fn target_block_num(&self) -> u64 {
        (self.ei_leaf_hi.get() as u64) << 32 | self.ei_leaf_lo.get() as u64
    }
}

#[cfg(test)]
mod test {
    use {
        super::{
            Extent, ExtentHeader, ExtentIndex, FeatureIncompat, ParseToStruct, ParsingError,
            SuperBlock, EH_MAGIC, FIRST_BG_PADDING, LEU16, LEU32, LEU64, REQUIRED_FEATURE_INCOMPAT,
            SB_MAGIC,
        },
        crate::readers::VecReader,
        std::{fs, sync::Arc},
    };

    impl Default for SuperBlock {
        fn default() -> SuperBlock {
            SuperBlock {
                e2fs_icount: LEU32::new(0),
                e2fs_bcount: LEU32::new(0),
                e2fs_rbcount: LEU32::new(0),
                e2fs_fbcount: LEU32::new(0),
                e2fs_ficount: LEU32::new(0),
                e2fs_first_dblock: LEU32::new(0),
                e2fs_log_bsize: LEU32::new(0),
                e2fs_log_fsize: LEU32::new(0),
                e2fs_bpg: LEU32::new(0),
                e2fs_fpg: LEU32::new(0),
                e2fs_ipg: LEU32::new(0),
                e2fs_mtime: LEU32::new(0),
                e2fs_wtime: LEU32::new(0),
                e2fs_mnt_count: LEU16::new(0),
                e2fs_max_mnt_count: LEU16::new(0),
                e2fs_magic: LEU16::new(0),
                e2fs_state: LEU16::new(0),
                e2fs_beh: LEU16::new(0),
                e2fs_minrev: LEU16::new(0),
                e2fs_lastfsck: LEU32::new(0),
                e2fs_fsckintv: LEU32::new(0),
                e2fs_creator: LEU32::new(0),
                e2fs_rev: LEU32::new(0),
                e2fs_ruid: LEU16::new(0),
                e2fs_rgid: LEU16::new(0),
                e2fs_first_ino: LEU32::new(0),
                e2fs_inode_size: LEU16::new(0),
                e2fs_block_group_nr: LEU16::new(0),
                e2fs_features_compat: LEU32::new(0),
                e2fs_features_incompat: LEU32::new(0),
                e2fs_features_rocompat: LEU32::new(0),
                e2fs_uuid: [0; 16],
                e2fs_vname: [0; 16],
                e2fs_fsmnt: [0; 64],
                e2fs_algo: LEU32::new(0),
                e2fs_prealloc: 0,
                e2fs_dir_prealloc: 0,
                e2fs_reserved_ngdb: LEU16::new(0),
                e3fs_journal_uuid: [0; 16],
                e3fs_journal_inum: LEU32::new(0),
                e3fs_journal_dev: LEU32::new(0),
                e3fs_last_orphan: LEU32::new(0),
                e3fs_hash_seed: [LEU32::new(0); 4],
                e3fs_def_hash_version: 0,
                e3fs_jnl_backup_type: 0,
                e3fs_desc_size: LEU16::new(0),
                e3fs_default_mount_opts: LEU32::new(0),
                e3fs_first_meta_bg: LEU32::new(0),
                e3fs_mkfs_time: LEU32::new(0),
                e3fs_jnl_blks: [LEU32::new(0); 17],
                e4fs_bcount_hi: LEU32::new(0),
                e4fs_rbcount_hi: LEU32::new(0),
                e4fs_fbcount_hi: LEU32::new(0),
                e4fs_min_extra_isize: LEU16::new(0),
                e4fs_want_extra_isize: LEU16::new(0),
                e4fs_flags: LEU32::new(0),
                e4fs_raid_stride: LEU16::new(0),
                e4fs_mmpintv: LEU16::new(0),
                e4fs_mmpblk: LEU64::new(0),
                e4fs_raid_stripe_wid: LEU32::new(0),
                e4fs_log_gpf: 0,
                e4fs_chksum_type: 0,
                e4fs_encrypt: 0,
                e4fs_reserved_pad: 0,
                e4fs_kbytes_written: LEU64::new(0),
                e4fs_snapinum: LEU32::new(0),
                e4fs_snapid: LEU32::new(0),
                e4fs_snaprbcount: LEU64::new(0),
                e4fs_snaplist: LEU32::new(0),
                e4fs_errcount: LEU32::new(0),
                e4fs_first_errtime: LEU32::new(0),
                e4fs_first_errino: LEU32::new(0),
                e4fs_first_errblk: LEU64::new(0),
                e4fs_first_errfunc: [0; 32],
                e4fs_first_errline: LEU32::new(0),
                e4fs_last_errtime: LEU32::new(0),
                e4fs_last_errino: LEU32::new(0),
                e4fs_last_errline: LEU32::new(0),
                e4fs_last_errblk: LEU64::new(0),
                e4fs_last_errfunc: [0; 32],
                e4fs_mount_opts: [0; 64],
                e4fs_usrquota_inum: LEU32::new(0),
                e4fs_grpquota_inum: LEU32::new(0),
                e4fs_overhead_clusters: LEU32::new(0),
                e4fs_backup_bgs: [LEU32::new(0); 2],
                e4fs_encrypt_algos: [0; 4],
                e4fs_encrypt_pw_salt: [0; 16],
                e4fs_lpf_ino: LEU32::new(0),
                e4fs_proj_quota_inum: LEU32::new(0),
                e4fs_chksum_seed: LEU32::new(0),
                e4fs_reserved: [LEU32::new(0); 98],
                e4fs_sbchksum: LEU32::new(0),
            }
        }
    }

    // NOTE: Impls for `INode` and `DirEntry2` depend on calculated data locations. Testing these
    // functions are being done in `parser.rs` where those locations are being calculated.

    // SuperBlock has a known data location and enables us to test `ParseToStruct` functions.

    /// Covers these functions:
    /// - ParseToStruct::{read_from_offset, to_struct_arc, to_struct_ref, validate}
    /// - SuperBlock::{block_size, check_magic}
    #[test]
    fn parse_superblock() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let reader = Arc::new(VecReader::new(data));
        let sb = SuperBlock::parse(reader).expect("Parsed Super Block");
        // Block size of the 1file.img is 1KiB.
        assert_eq!(sb.block_size().unwrap(), FIRST_BG_PADDING);

        // Validate magic number.
        assert!(sb.check_magic().is_ok());

        let mut sb = SuperBlock::default();
        assert!(sb.check_magic().is_err());

        sb.e2fs_magic = LEU16::new(SB_MAGIC);
        assert!(sb.check_magic().is_ok());

        // Validate block size.
        sb.e2fs_log_bsize = LEU32::new(0); // 1KiB
        assert!(sb.block_size().is_ok());
        sb.e2fs_log_bsize = LEU32::new(1); // 2KiB
        assert!(sb.block_size().is_ok());
        sb.e2fs_log_bsize = LEU32::new(2); // 4KiB
        assert!(sb.block_size().is_ok());
        sb.e2fs_log_bsize = LEU32::new(6); // 64KiB
        assert!(sb.block_size().is_ok());

        // Others are disallowed, checking values neighboring valid ones.
        sb.e2fs_log_bsize = LEU32::new(3);
        assert!(sb.block_size().is_err());
        sb.e2fs_log_bsize = LEU32::new(5);
        assert!(sb.block_size().is_err());
        sb.e2fs_log_bsize = LEU32::new(7);
        assert!(sb.block_size().is_err());
        // How exhaustive do we get?
        sb.e2fs_log_bsize = LEU32::new(20);
        assert!(sb.block_size().is_err());
    }

    /// Covers ParseToStruct::parse_offset.
    #[test]
    fn parse_to_struct_parse_offset() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let reader = Arc::new(VecReader::new(data));
        let sb = SuperBlock::parse_offset(
            reader,
            FIRST_BG_PADDING,
            ParsingError::InvalidSuperBlock(FIRST_BG_PADDING),
        )
        .expect("Parsed Super Block");
        assert!(sb.check_magic().is_ok());
    }

    /// Covers SuperBlock::feature_check
    #[test]
    fn incompatible_feature_flags() {
        let data = fs::read("/pkg/data/1file.img").expect("Unable to read file");
        let reader = Arc::new(VecReader::new(data));
        let sb = SuperBlock::parse(reader).expect("Parsed Super Block");
        assert_eq!(sb.e2fs_magic.get(), SB_MAGIC);
        assert!(sb.feature_check().is_ok());

        let mut sb = SuperBlock::default();
        match sb.feature_check() {
            Ok(_) => assert!(false, "Feature flags should be incorrect."),
            Err(e) => assert_eq!(
                format!("{}", e),
                format!(
                    "Required feature flags (feature_incompat): 0x{:X}",
                    REQUIRED_FEATURE_INCOMPAT
                )
            ),
        }

        // Test that an exact match is not necessary.
        sb.e2fs_features_incompat = LEU32::new(REQUIRED_FEATURE_INCOMPAT | 0xF00000);
        assert!(sb.feature_check().is_ok());

        // Test can report subset.
        sb.e2fs_features_incompat = LEU32::new(FeatureIncompat::Extents as u32);
        match sb.feature_check() {
            Ok(_) => assert!(false, "Feature flags should be incorrect."),
            Err(e) => assert_eq!(
                format!("{}", e),
                format!(
                    "Required feature flags (feature_incompat): 0x{:X}",
                    REQUIRED_FEATURE_INCOMPAT ^ FeatureIncompat::Extents as u32
                )
            ),
        }

        // Test banned flag.
        sb.e2fs_features_incompat = LEU32::new(FeatureIncompat::Is64Bit as u32);
        match sb.feature_check() {
            Ok(_) => assert!(false, "Feature flags should be incorrect."),
            Err(e) => assert_eq!(
                format!("{}", e),
                format!(
                    "Incompatible feature flags (feature_incompat): 0x{:X}",
                    FeatureIncompat::Is64Bit as u32
                )
            ),
        }
    }

    /// Covers Extent::target_block_num.
    #[test]
    fn extent_target_block_num() {
        let e = Extent {
            e_blk: LEU32::new(0),
            e_len: LEU16::new(0),
            e_start_hi: LEU16::new(0x4444),
            e_start_lo: LEU32::new(0x6666_8888),
        };
        assert_eq!(e.target_block_num(), 0x4444_6666_8888);
    }

    /// Covers ExtentIndex::target_block_num.
    #[test]
    fn extent_index_target_block_num() {
        let e = ExtentIndex {
            ei_blk: LEU32::new(0),
            ei_leaf_lo: LEU32::new(0x6666_8888),
            ei_leaf_hi: LEU16::new(0x4444),
            ei_unused: LEU16::new(0),
        };
        assert_eq!(e.target_block_num(), 0x4444_6666_8888);
    }

    /// Covers ExtentHeader::check_magic.
    #[test]
    fn extent_header_check_magic() {
        let e = ExtentHeader {
            eh_magic: LEU16::new(EH_MAGIC),
            eh_ecount: LEU16::new(0),
            eh_max: LEU16::new(0),
            eh_depth: LEU16::new(0),
            eh_gen: LEU32::new(0),
        };
        assert!(e.check_magic().is_ok());

        let e = ExtentHeader {
            eh_magic: LEU16::new(0x1234),
            eh_ecount: LEU16::new(0),
            eh_max: LEU16::new(0),
            eh_depth: LEU16::new(0),
            eh_gen: LEU32::new(0),
        };
        assert!(e.check_magic().is_err());
    }
}
