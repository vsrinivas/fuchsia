// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use std::collections::HashMap;
use std::sync::Arc;
use std::ops::Deref;
use parking_lot::RwLock;
pub use starnix_macros::FileDesc;

use crate::types::*;
use crate::ThreadContext;

pub type FdNumber = i32;

/// Corresponds to struct file_operations in Linux, plus any filesystem-specific data.
pub trait FileDesc: Deref<Target=FileCommon> {
    fn write(&self, ctx: &ThreadContext, data: &[iovec]) -> Result<usize, Errno>;
    fn read(&self, ctx: &ThreadContext, offset: &mut usize, data: &[iovec]) -> Result<usize, Errno>;
    fn mmap(&self, _ctx: &ThreadContext, _prot: zx::VmarFlags, _flags: i32, _offset: usize) -> Result<(zx::Vmo, usize), Errno> {
        Err(ENODEV)
    }
    // TODO(tbodt): This is actually an inode operation, and is only here because we don't have
    // such a thing yet. Will need to be moved.
    fn fstat(&self, ctx: &ThreadContext) -> Result<stat_t, Errno>;
}

/// Corresponds to struct file in Linux.
#[derive(Default)]
pub struct FileCommon {
}

pub type FdHandle = Arc<dyn FileDesc>;

pub struct FdTable {
    table: RwLock<HashMap<FdNumber, FdHandle>>,
}

impl FdTable {
    pub fn new() -> FdTable {
        FdTable { table: RwLock::new(HashMap::new()) }
    }

    pub fn install_fd(&self, fd: FdHandle) -> Result<FdNumber, Errno> {
        let mut table = self.table.write();
        let mut n = 0;
        while table.contains_key(&n) {
            n += 1;
        }
        table.insert(n, fd);
        Ok(n)
    }

    pub fn get(&self, n: FdNumber) -> Result<FdHandle, Errno> {
        let table = self.table.read();
        table.get(&n).map(|handle| handle.clone()).ok_or_else(|| EBADF)
    }
}
