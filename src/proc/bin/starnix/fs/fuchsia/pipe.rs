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
    pub fn from_socket(kern: &Kernel, socket: zx::Socket) -> Result<FileHandle, zx::Status> {
        // TODO: Distinguish between stream and datagram sockets.
        Ok(Anon::new_file(kern, FuchsiaPipe { socket }, AnonNodeType::Misc))
    }
}

impl FileOps for FuchsiaPipe {
    fd_impl_nonseekable!();

    fn write(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut size = 0;
        task.mm.read_each(data, |bytes| {
            let actual = self.socket.write(&bytes).map_err(Errno::from_status_like_fdio)?;
            size += actual;
            if actual != bytes.len() {
                return Ok(None);
            }
            Ok(Some(()))
        })?;
        Ok(size)
    }

    fn read(&self, _file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        let mut size = 0;
        task.mm.write_each(data, |bytes| {
            let actual = self.socket.read(bytes).map_err(Errno::from_status_like_fdio)?;
            size += actual;
            Ok(&bytes[0..actual])
        })?;
        Ok(size)
    }

    fn fstat(&self, _file: &FileObject, _task: &Task) -> Result<stat_t, Errno> {
        Err(ENOSYS)
    }
}
