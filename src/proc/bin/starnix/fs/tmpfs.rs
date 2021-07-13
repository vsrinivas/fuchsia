// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use super::*;
use crate::devices::*;
use crate::fd_impl_seekable;
use crate::mm::PAGE_SIZE;
use crate::task::*;
use crate::types::*;
use fuchsia_zircon::{self as zx};
use std::sync::Arc;
use zx::VmoOptions;

pub struct TmpfsDirectory;

impl FsNodeOps for TmpfsDirectory {
    fn mkdir(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(Self))
    }
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Err(ENOSYS)
    }
    fn create(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Ok(Box::new(TmpfsFileNode::new()?))
    }
}

pub fn new_tmpfs() -> FsNodeHandle {
    let tmpfs_dev = AnonNodeDevice::new(0);
    FsNode::new_root(TmpfsDirectory, tmpfs_dev)
}

struct TmpfsFileNode {
    vmo: Arc<zx::Vmo>,
}

impl TmpfsFileNode {
    fn new() -> Result<TmpfsFileNode, Errno> {
        let vmo = zx::Vmo::create_with_opts(VmoOptions::RESIZABLE, 0).map_err(|_| ENOMEM)?;
        Ok(TmpfsFileNode { vmo: Arc::new(vmo) })
    }
}

impl FsNodeOps for TmpfsFileNode {
    fn open(&self, _node: &FsNode) -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(TmpfsFileObject { vmo: self.vmo.clone() }))
    }

    fn create(&self, _node: &FsNode, _name: &FsStr) -> Result<Box<dyn FsNodeOps>, Errno> {
        Err(ENOTDIR)
    }
}

struct TmpfsFileObject {
    vmo: Arc<zx::Vmo>,
}

impl FileOps for TmpfsFileObject {
    fd_impl_seekable!();

    fn read_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut state = file.node().state_mut();
        let file_length = state.size;
        let want_read = UserBuffer::get_total_length(data);
        let to_read =
            if file_length < offset + want_read { file_length - offset } else { want_read };
        let mut buf = vec![0u8; to_read];
        self.vmo.read(&mut buf[..], offset as u64).map_err(|_| EIO)?;
        // TODO(steveaustin) - write_each might might be more efficient
        task.mm.write_all(data, &mut buf[..])?;
        state.time_access = fuchsia_runtime::utc_time();
        Ok(to_read)
    }

    fn write_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut state = file.node().state_mut();
        let want_write = UserBuffer::get_total_length(data);
        let write_end = offset + want_write;
        let mut update_content_size = false;
        if write_end > state.size {
            if write_end > state.storage_size {
                let mut new_size = write_end as u64;
                // TODO(steveaustin) move the padding logic
                // to a library where it can be shared with
                // similar code in pipe
                let padding = new_size as u64 % *PAGE_SIZE;
                if padding > 0 {
                    new_size += padding;
                }
                self.vmo.set_size(new_size).map_err(|_| ENOMEM)?;
                state.storage_size = new_size as usize;
            }
            update_content_size = true;
        }

        let mut buf = vec![0u8; want_write];
        task.mm.read_all(data, &mut buf[..])?;
        self.vmo.write(&mut buf[..], offset as u64).map_err(|_| EIO)?;
        if update_content_size {
            state.size = write_end;
        }
        let now = fuchsia_runtime::utc_time();
        state.time_access = now;
        state.time_modify = now;
        Ok(want_write)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_async as fasync;
    use zerocopy::AsBytes;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_write_read() {
        let fs = new_tmpfs();
        let fs_context = FsContext::new(Namespace::new(fs.clone()));
        let (_kernel, task_owner) = create_kernel_and_task_with_fs(fs_context);

        let mm = &task_owner.task.mm;
        let test_mem_size = 0x10000;
        let test_vmo = zx::Vmo::create(test_mem_size).unwrap();

        let path = b"test.bin";
        let _file_node = fs.create(path).unwrap();

        let wr_file = task_owner.task.open_file(path, OpenFlags::RDWR).unwrap();

        let flags = zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
        let test_addr =
            mm.map(UserAddress::default(), test_vmo, 0, test_mem_size as usize, flags).unwrap();

        let seq_addr = UserAddress::from_ptr(test_addr.ptr() + path.len());
        let test_seq = 0..10000u16;
        let test_vec = test_seq.collect::<Vec<_>>();
        let test_bytes = test_vec.as_slice().as_bytes();
        mm.write_memory(seq_addr, test_bytes).unwrap();
        let buf = [UserBuffer { address: seq_addr, length: test_bytes.len() }];

        let written = wr_file.write(&task_owner.task, &buf).unwrap();
        assert_eq!(written, test_bytes.len());

        let mut read_vec = vec![0u8; test_bytes.len()];
        mm.read_memory(seq_addr, read_vec.as_bytes_mut()).unwrap();

        assert_eq!(test_bytes, &*read_vec);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_permissions() {
        let fs_context = FsContext::new(Namespace::new(new_tmpfs()));
        let (_kernel, task_owner) = create_kernel_and_task_with_fs(fs_context);
        let task = &task_owner.task;

        let path = b"test.bin";
        let file = task.open_file(path, OpenFlags::CREAT).expect("failed to create file");
        assert_eq!(0, file.read(task, &[]).expect("failed to read"));
        assert!(file.write(task, &[]).is_err());

        let file = task.open_file(path, OpenFlags::WRONLY).expect("failed to open file WRONLY");
        assert!(file.read(task, &[]).is_err());
        assert_eq!(0, file.write(task, &[]).expect("failed to write"));

        let file = task.open_file(path, OpenFlags::RDWR).expect("failed to open file RDWR");
        assert_eq!(0, file.read(task, &[]).expect("failed to read"));
        assert_eq!(0, file.write(task, &[]).expect("failed to write"));
    }
}
