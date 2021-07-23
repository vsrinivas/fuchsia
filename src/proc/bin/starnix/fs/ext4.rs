// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ext4_read_only::parser::Parser as ExtParser;
use ext4_read_only::readers::VmoReader as ExtVmoReader;
use ext4_read_only::structs as ext_structs;
use fuchsia_zircon::{self as zx};
use once_cell::sync::OnceCell;
use std::sync::{Arc, Weak};

use super::*;
use crate::devices::AnonNodeDevice;
use crate::logging::impossible_error;
use crate::types::*;

pub struct ExtFilesystem {
    parser: ExtParser<ExtVmoReader>,
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
        let fs = Arc::new(Self {
            parser: ExtParser::new(ExtVmoReader::new(Arc::new(fidl_fuchsia_mem::Buffer {
                size,
                vmo,
            }))),
        });
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
                self.inner.fs().parser.read_file(self.inner.inode_num).map_err(ext_error)?;
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
