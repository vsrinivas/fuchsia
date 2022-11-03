// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased};
use std::sync::Arc;

use super::*;
use crate::logging::impossible_error;
use crate::mm::vmo::round_up_to_system_page_size;
use crate::mm::MemoryAccessorExt;
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::CurrentTask;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

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

    pub fn from_bytes(data: &[u8]) -> Result<VmoFileNode, Errno> {
        let vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, data.len() as u64)
            .map_err(|_| errno!(ENOMEM))?;
        vmo.write(data, 0).map_err(|_| errno!(ENOMEM))?;
        Ok(VmoFileNode { vmo: Arc::new(vmo), xattrs: MemoryXattrStorage::default() })
    }
}

impl FsNodeOps for VmoFileNode {
    fs_node_impl_xattr_delegate!(self, self.xattrs);

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
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
        vmo: &zx::Vmo,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let actual = {
            let info = file.node().info();
            let file_length = info.size;
            let want_read = UserBuffer::get_total_length(data)?;
            if want_read > MAX_LFS_FILESIZE - offset {
                return error!(EINVAL);
            }
            if offset < file_length {
                let to_read =
                    if file_length < offset + want_read { file_length - offset } else { want_read };
                let mut buf = vec![0u8; to_read];
                vmo.read(&mut buf[..], offset as u64).map_err(|_| errno!(EIO))?;
                drop(info);
                // TODO(steveaustin) - write_each might might be more efficient
                current_task.mm.write_all(data, &buf[..])?;
                to_read
            } else {
                0
            }
        };
        // TODO(steveaustin) - omit updating time_access to allow info to be immutable
        // and thus allow simultaneous reads.
        file.node().info_write().time_access = fuchsia_runtime::utc_time();
        Ok(actual)
    }

    pub fn write_at(
        vmo: &zx::Vmo,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let want_write = UserBuffer::get_total_length(data)?;
        if want_write > MAX_LFS_FILESIZE - offset {
            return error!(EINVAL);
        }
        let mut buf = vec![0u8; want_write];
        current_task.mm.read_all(data, &mut buf[..])?;

        let mut info = file.node().info_write();
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
        vmo.write(&buf[..], offset as u64).map_err(|_| errno!(EIO))?;

        if update_content_size {
            info.size = write_end;
        }
        let now = fuchsia_runtime::utc_time();
        info.time_access = now;
        info.time_modify = now;
        Ok(want_write)
    }

    pub fn get_vmo(
        vmo: &zx::Vmo,
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
    fileops_impl_seekable!();
    fileops_impl_nonblocking!();

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

    fn fcntl(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        cmd: u32,
        arg: u64,
    ) -> Result<SyscallResult, Errno> {
        match cmd {
            // Fake SEAL ioctl
            // TODO(fxb/103801): Implements the operation.
            F_ADD_SEALS | F_GET_SEALS => Ok(SUCCESS),
            _ => default_fcntl(current_task, file, cmd, arg),
        }
    }
}

pub fn new_memfd(current_task: &CurrentTask, flags: OpenFlags) -> Result<FileHandle, Errno> {
    let fs = anon_fs(current_task.kernel());
    let node =
        fs.create_node_with_ops(VmoFileNode::new()?, mode!(IFREG, 0o600), current_task.as_fscred());
    Ok(FileObject::new_anonymous(node.open(current_task, flags, false)?, node, flags))
}
