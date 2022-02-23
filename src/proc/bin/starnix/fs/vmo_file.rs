// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased};
use parking_lot::Mutex;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::sync::Arc;

use super::*;
use crate::logging::impossible_error;
use crate::mm::vmo::round_up_to_system_page_size;
use crate::task::CurrentTask;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;
use crate::{errno, error, fd_impl_nonblocking, fd_impl_seekable, fs_node_impl_xattr_delegate};

#[derive(Default)]
struct MemoryXattrStorage {
    xattrs: Mutex<HashMap<FsString, FsString>>,
}
impl MemoryXattrStorage {
    fn set_xattr(&self, name: &FsStr, value: &FsStr, op: XattrOp) -> Result<(), Errno> {
        let mut xattrs = self.xattrs.lock();
        let entry = xattrs.entry(name.to_owned());
        match entry {
            Entry::Vacant(_) if op == XattrOp::Replace => return error!(ENODATA),
            Entry::Occupied(_) if op == XattrOp::Create => return error!(EEXIST),
            Entry::Vacant(v) => {
                v.insert(value.to_owned());
            }
            Entry::Occupied(mut o) => {
                let s = o.get_mut();
                s.clear();
                s.extend_from_slice(value);
            }
        };
        Ok(())
    }
}

pub struct VmoFileNode {
    /// The memory that backs this file.
    vmo: Arc<zx::Vmo>,
    xattrs: MemoryXattrStorage,
}

impl VmoFileNode {
    pub fn new() -> Result<VmoFileNode, Errno> {
        let vmo =
            zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, 0).map_err(|_| errno!(ENOMEM))?;
        Ok(VmoFileNode { vmo: Arc::new(vmo), xattrs: MemoryXattrStorage::default() })
    }
}

impl FsNodeOps for VmoFileNode {
    fs_node_impl_xattr_delegate!(self, self.xattrs);

    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(VmoFileObject::new(self.vmo.clone())))
    }

    fn truncate(&self, node: &FsNode, length: u64) -> Result<(), Errno> {
        let mut info = node.info_write();
        if info.size == length as usize {
            return Ok(());
        }
        self.vmo.set_size(length).map_err(|status| match status {
            zx::Status::NO_MEMORY => errno!(ENOMEM),
            zx::Status::OUT_OF_RANGE => errno!(ENOMEM),
            _ => impossible_error(status),
        })?;
        info.size = length as usize;
        info.storage_size = self.vmo.get_size().map_err(impossible_error)? as usize;
        let time = fuchsia_runtime::utc_time();
        info.time_access = time;
        info.time_modify = time;
        Ok(())
    }
}

pub struct VmoFileObject {
    pub vmo: Arc<zx::Vmo>,
}

impl VmoFileObject {
    pub fn new(vmo: Arc<zx::Vmo>) -> Self {
        VmoFileObject { vmo }
    }

    pub fn read_at(
        vmo: &Arc<zx::Vmo>,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut info = file.node().info_write();
        let file_length = info.size;
        let want_read = UserBuffer::get_total_length(data)?;
        if want_read > MAX_LFS_FILESIZE - offset {
            return error!(EINVAL);
        }
        let actual = if offset < file_length {
            let to_read =
                if file_length < offset + want_read { file_length - offset } else { want_read };
            let mut buf = vec![0u8; to_read];
            vmo.read(&mut buf[..], offset as u64).map_err(|_| errno!(EIO))?;
            // TODO(steveaustin) - write_each might might be more efficient
            current_task.mm.write_all(data, &mut buf[..])?;
            to_read
        } else {
            0
        };
        // TODO(steveaustin) - omit updating time_access to allow info to be immutable
        // and thus allow simultaneous reads.
        info.time_access = fuchsia_runtime::utc_time();
        Ok(actual)
    }

    pub fn write_at(
        vmo: &Arc<zx::Vmo>,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut info = file.node().info_write();
        let want_write = UserBuffer::get_total_length(data)?;
        if want_write > MAX_LFS_FILESIZE - offset {
            return error!(EINVAL);
        }
        let write_end = offset + want_write;
        let mut update_content_size = false;
        if write_end > info.size {
            if write_end > info.storage_size {
                let new_size = round_up_to_system_page_size(write_end)?;
                vmo.set_size(new_size as u64).map_err(|_| errno!(ENOMEM))?;
                info.storage_size = new_size as usize;
            }
            update_content_size = true;
        }

        let mut buf = vec![0u8; want_write];
        current_task.mm.read_all(data, &mut buf[..])?;
        vmo.write(&mut buf[..], offset as u64).map_err(|_| errno!(EIO))?;
        if update_content_size {
            info.size = write_end;
        }
        let now = fuchsia_runtime::utc_time();
        info.time_access = now;
        info.time_modify = now;
        Ok(want_write)
    }

    pub fn get_vmo(
        vmo: &Arc<zx::Vmo>,
        _file: &FileObject,
        _current_task: &CurrentTask,
        prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        let mut vmo = vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(impossible_error)?;
        if prot.contains(zx::VmarFlags::PERM_EXECUTE) {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        Ok(vmo)
    }
}

impl FileOps for VmoFileObject {
    fd_impl_seekable!();
    fd_impl_nonblocking!();

    fn read_at(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        VmoFileObject::read_at(&self.vmo, file, current_task, offset, data)
    }

    fn write_at(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        VmoFileObject::write_at(&self.vmo, file, current_task, offset, data)
    }

    fn get_vmo(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        _length: Option<usize>,
        prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        VmoFileObject::get_vmo(&self.vmo, file, current_task, prot)
    }
}
