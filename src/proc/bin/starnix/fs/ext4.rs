// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ext4_read_only::parser::Parser as ExtParser;
use ext4_read_only::readers::{self as ext4_readers, VmoReader as ExtVmoReader};
use ext4_read_only::structs as ext_structs;
use fuchsia_zircon::{self as zx};
use once_cell::sync::OnceCell;
use std::collections::BTreeMap;
use std::mem::size_of_val;
use std::sync::{Arc, Weak};
use zerocopy::{AsBytes, FromBytes};

use super::*;
use crate::devices::AnonNodeDevice;
use crate::logging::impossible_error;
use crate::types::*;

pub struct ExtFilesystem {
    parser: ExtParser<AndroidSparseReader<ExtVmoReader>>,
}
impl FileSystemOps for Arc<ExtFilesystem> {}

struct ExtNode {
    fs: Weak<ExtFilesystem>,
    inode_num: u32,
    inode: Arc<ext_structs::INode>,
}

impl ExtFilesystem {
    #[allow(dead_code)]
    pub fn new(vmo: zx::Vmo) -> Result<FileSystemHandle, Errno> {
        let size = vmo.get_size().map_err(|_| EIO)?;
        let vmo_reader = ExtVmoReader::new(Arc::new(fidl_fuchsia_mem::Buffer { vmo, size }));
        let parser = ExtParser::new(AndroidSparseReader::new(vmo_reader).map_err(|_| EIO)?);
        let fs = Arc::new(Self { parser });
        let root = FsNode::new_root(
            ExtDirectory { inner: ExtNode::new(fs.clone(), ext_structs::ROOT_INODE_NUM)? },
            AnonNodeDevice::new(0),
        );
        Ok(FileSystem::new(fs, root))
    }
}

impl ExtNode {
    fn new(fs: Arc<ExtFilesystem>, inode_num: u32) -> Result<ExtNode, Errno> {
        let inode = fs.parser.inode(inode_num).map_err(ext_error)?;
        Ok(ExtNode { fs: Arc::downgrade(&fs), inode_num, inode })
    }

    fn fs(&self) -> Arc<ExtFilesystem> {
        self.fs.upgrade().unwrap()
    }
}

struct ExtDirectory {
    inner: ExtNode,
}

impl FsNodeOps for ExtDirectory {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        // TODO(tbodt): Implement opening directories.
        Err(ENOSYS)
    }

    fn lookup(&self, _node: &FsNode, mut child: FsNode) -> Result<FsNodeHandle, Errno> {
        let dir_entries =
            self.inner.fs().parser.entries_from_inode(&self.inner.inode).map_err(ext_error)?;
        let entry =
            dir_entries.iter().find(|e| e.name_bytes() == child.local_name()).ok_or(ENOENT)?;
        let node = ExtNode::new(self.inner.fs(), entry.e2d_ino.into())?;

        let info = child.info_mut();
        info.inode_num = node.inode_num as u64;
        info.mode = FileMode::from_bits(node.inode.e2di_mode.into());
        info.uid = node.inode.e2di_uid.into();
        info.gid = node.inode.e2di_gid.into();
        info.size = u32::from(node.inode.e2di_size) as usize;
        info.link_count = node.inode.e2di_nlink.into();

        let entry_type = ext_structs::EntryType::from_u8(entry.e2d_type).map_err(ext_error)?;
        match entry_type {
            ext_structs::EntryType::RegularFile => child.set_ops(ExtFile::new(node)),
            ext_structs::EntryType::Directory => child.set_ops(ExtDirectory { inner: node }),
            _ => {
                log::warn!("unhandled ext entry type {:?}", entry_type);
                child.set_ops(ExtFile::new(node))
            }
        };

        Ok(child.into_handle())
    }
}

struct ExtFile {
    inner: ExtNode,
    vmo: OnceCell<Arc<zx::Vmo>>,
}

impl ExtFile {
    fn new(inner: ExtNode) -> Self {
        ExtFile { inner, vmo: OnceCell::new() }
    }
}

impl FsNodeOps for ExtFile {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        let vmo = self.vmo.get_or_try_init(|| {
            let bytes =
                self.inner.fs().parser.read_data(self.inner.inode_num).map_err(ext_error)?;
            let vmo = zx::Vmo::create(bytes.len() as u64).map_err(vmo_error)?;
            vmo.write(&bytes, 0).map_err(vmo_error)?;
            Ok(Arc::new(vmo))
        })?;
        // TODO(tbodt): this file will be writable (though changes don't persist once you close the
        // file)
        Ok(Box::new(VmoFileObject::new(vmo.clone())))
    }
}

fn ext_error(err: ext_structs::ParsingError) -> Errno {
    log::error!("ext4 error: {:?}", err);
    EIO
}

fn vmo_error(err: zx::Status) -> Errno {
    match err {
        zx::Status::NO_MEMORY => ENOMEM,
        _ => impossible_error(err),
    }
}

struct AndroidSparseReader<R: ext4_readers::Reader> {
    inner: R,
    header: SparseHeader,
    chunks: BTreeMap<usize, SparseChunk>,
}

/// Copied from system/core/libsparse/sparse_format.h
#[derive(AsBytes, FromBytes, Default, Debug)]
#[repr(C)]
struct SparseHeader {
    /// 0xed26ff3a
    magic: u32,
    /// (0x1) - reject images with higher major versions
    major_version: u16,
    /// (0x0) - allow images with higer minor versions
    minor_version: u16,
    /// 28 bytes for first revision of the file format
    file_hdr_sz: u16,
    /// 12 bytes for first revision of the file format
    chunk_hdr_sz: u16,
    /// block size in bytes, must be a multiple of 4 (4096)
    blk_sz: u32,
    /// total blocks in the non-sparse output image
    total_blks: u32,
    /// total chunks in the sparse input image
    total_chunks: u32,
    /// CRC32 checksum of the original data, counting "don't care"
    /// as 0. Standard 802.3 polynomial, use a Public Domain
    /// table implementation
    image_checksum: u32,
}

const SPARSE_HEADER_MAGIC: u32 = 0xed26ff3a;

/// Copied from system/core/libsparse/sparse_format.h
#[derive(AsBytes, FromBytes, Default)]
#[repr(C)]
struct RawChunkHeader {
    /// 0xCAC1 -> raw; 0xCAC2 -> fill; 0xCAC3 -> don't care
    chunk_type: u16,
    _reserved: u16,
    /// in blocks in output image
    chunk_sz: u32,
    /// in bytes of chunk input file including chunk header and data
    total_sz: u32,
}

#[derive(Debug)]
enum SparseChunk {
    Raw { in_offset: usize, in_size: usize },
    Fill { fill: [u8; 4] },
    DontCare,
}

const CHUNK_TYPE_RAW: u16 = 0xCAC1;
const CHUNK_TYPE_FILL: u16 = 0xCAC2;
const CHUNK_TYPE_DONT_CARE: u16 = 0xCAC3;

impl<R: ext4_readers::Reader> AndroidSparseReader<R> {
    fn new(inner: R) -> Result<Self, anyhow::Error> {
        let mut header = SparseHeader::default();
        inner.read(0, header.as_bytes_mut())?;
        let mut chunks = BTreeMap::new();
        if header.magic == SPARSE_HEADER_MAGIC {
            if header.major_version != 1 {
                anyhow::bail!("unknown sparse image major version {}", header.major_version);
            }
            let mut in_offset = size_of_val(&header);
            let mut out_offset = 0;
            for _ in 0..header.total_chunks {
                let mut chunk_header = RawChunkHeader::default();
                inner.read(in_offset, chunk_header.as_bytes_mut())?;
                let data_offset = in_offset + size_of_val(&chunk_header);
                let data_size = chunk_header.total_sz as usize - size_of_val(&chunk_header);
                in_offset += chunk_header.total_sz as usize;
                let chunk_out_offset = out_offset;
                out_offset += chunk_header.chunk_sz as usize * header.blk_sz as usize;
                let chunk = match chunk_header.chunk_type {
                    CHUNK_TYPE_RAW => {
                        SparseChunk::Raw { in_offset: data_offset, in_size: data_size }
                    }
                    CHUNK_TYPE_FILL => {
                        let mut fill = [0u8; 4];
                        if data_size != size_of_val(&fill) {
                            anyhow::bail!(
                                "fill chunk of sparse image is the wrong size: {}, should be {}",
                                data_size,
                                size_of_val(&fill),
                            );
                        }
                        inner.read(data_offset, fill.as_bytes_mut())?;
                        SparseChunk::Fill { fill }
                    }
                    CHUNK_TYPE_DONT_CARE | _ => SparseChunk::DontCare,
                };
                chunks.insert(chunk_out_offset, chunk);
            }
        }
        Ok(Self { inner, header, chunks })
    }
}

impl<R: ext4_readers::Reader> ext4_readers::Reader for AndroidSparseReader<R> {
    fn read(&self, offset: usize, data: &mut [u8]) -> Result<(), ext4_readers::ReaderError> {
        if self.header.magic != SPARSE_HEADER_MAGIC {
            return self.inner.read(offset, data);
        }
        let total_size = self.header.total_blks as usize * self.header.blk_sz as usize;

        let (chunk_start, chunk) = match self.chunks.range(..offset + 1).next_back() {
            Some(x) => x,
            _ => return Err(ext4_readers::ReaderError::OutOfBounds(offset, total_size)),
        };
        match chunk {
            SparseChunk::Raw { in_offset, in_size } => {
                let chunk_offset = offset - chunk_start;
                if chunk_offset > *in_size {
                    return Err(ext4_readers::ReaderError::OutOfBounds(chunk_offset, total_size));
                }
                self.inner.read(in_offset + chunk_offset, data)?;
            }
            SparseChunk::Fill { fill } => {
                for i in offset..offset + data.len() {
                    data[i - offset] = fill[offset % fill.len()];
                }
            }
            SparseChunk::DontCare => {}
        }
        Ok(())
    }
}
