// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fs::*;
use crate::task::*;
use crate::types::*;
use crate::{errno, error, fd_impl_seekable, mode};

struct SeLinuxFs;
impl FileSystemOps for SeLinuxFs {}
impl SeLinuxFs {
    fn new() -> Result<FileSystemHandle, Errno> {
        let fs = FileSystem::new_with_permanent_entries(SeLinuxFs);
        fs.set_root(ROMemoryDirectory);
        let root = fs.root();
        root.add_node_ops(b"load", mode!(IFREG, 0600), SimpleFileNode::new(|| Ok(SeLoad)))?;
        root.add_node_ops(b"enforce", mode!(IFREG, 0600), SimpleFileNode::new(|| Ok(SeEnforce)))?;
        root.add_node_ops(
            b"checkreqprot",
            mode!(IFREG, 0600),
            SimpleFileNode::new(|| Ok(SeCheckReqProt)),
        )?;
        Ok(fs)
    }
}

struct SeLoad;
impl FileOps for SeLoad {
    fd_impl_seekable!();
    fn write_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if offset != 0 {
            return error!(EINVAL);
        }
        let size = UserBuffer::get_total_length(data);
        let mut buf = vec![0u8; size];
        task.mm.read_all(&data, &mut buf)?;
        log::info!("got selinux policy, length {}, ignoring", size);
        Ok(size)
    }

    fn read_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(ENOSYS)
    }
}

struct SeEnforce;
impl FileOps for SeEnforce {
    fd_impl_seekable!();
    fn write_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if offset != 0 {
            return error!(EINVAL);
        }
        let size = UserBuffer::get_total_length(data);
        let mut buf = vec![0u8; size];
        task.mm.read_all(&data, &mut buf)?;
        let enforce = parse_int(&buf)?;
        log::info!("setenforce: {}", enforce);
        Ok(size)
    }
    fn read_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(ENOSYS)
    }
}

struct SeCheckReqProt;
impl FileOps for SeCheckReqProt {
    fd_impl_seekable!();
    fn write_at(
        &self,
        _file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if offset != 0 {
            return error!(EINVAL);
        }
        let size = UserBuffer::get_total_length(data);
        let mut buf = vec![0u8; size];
        task.mm.read_all(&data, &mut buf)?;
        let checkreqprot = parse_int(&buf)?;
        log::info!("checkreqprot: {}", checkreqprot);
        Ok(size)
    }
    fn read_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(ENOSYS)
    }
}

fn parse_int(buf: &[u8]) -> Result<u32, Errno> {
    let i = buf.iter().position(|c| !char::from(*c).is_digit(10)).unwrap_or(buf.len());
    std::str::from_utf8(&buf[..i]).unwrap().parse::<u32>().map_err(|_| errno!(EINVAL))
}

pub fn selinux_fs(kern: &Kernel) -> &FileSystemHandle {
    kern.selinux_fs.get_or_init(|| SeLinuxFs::new().expect("failed to construct selinuxfs"))
}
