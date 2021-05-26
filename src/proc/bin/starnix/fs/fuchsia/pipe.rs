// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;

use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::task::*;
use crate::types::*;

pub struct FuchsiaPipe {
    socket: zx::Socket,
}

impl FuchsiaPipe {
    pub fn from_socket(socket: zx::Socket) -> Result<FileHandle, zx::Status> {
        // TODO: Distinguish between stream and datagram sockets.
        Ok(FileObject::new(FuchsiaPipe { socket }))
    }
}

impl FileOps for FuchsiaPipe {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, task: &Task, data: &[iovec_t]) -> Result<usize, Errno> {
        let mut size = 0;
        for vec in data {
            let mut local = vec![0; vec.iov_len];
            task.mm.read_memory(vec.iov_base, &mut local)?;
            let actual = self.socket.write(&local).map_err(Errno::from_status_like_fdio)?;
            size += actual;
            if actual != vec.iov_len {
                break;
            }
        }
        Ok(size)
    }

    fn read(&self, _file: &FileObject, task: &Task, data: &[iovec_t]) -> Result<usize, Errno> {
        let mut size = 0;
        for vec in data {
            let mut local = vec![0; vec.iov_len];
            let actual = self.socket.read(&mut local).map_err(Errno::from_status_like_fdio)?;
            task.mm.write_memory(vec.iov_base, &local[0..actual])?;
            size += actual;
            if actual != vec.iov_len {
                break;
            }
        }
        Ok(size)
    }

    fn fstat(&self, _file: &FileObject, _task: &Task) -> Result<stat_t, Errno> {
        Err(ENOSYS)
    }
}
