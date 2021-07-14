// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased};
use std::sync::Arc;

use super::*;
use crate::fd_impl_seekable;
use crate::logging::impossible_error;
use crate::mm::PAGE_SIZE;
use crate::task::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;

pub struct VmoFileObject {
    vmo: Arc<zx::Vmo>,
}

impl VmoFileObject {
    pub fn new(vmo: Arc<zx::Vmo>) -> Self {
        VmoFileObject { vmo }
    }
}

impl FileOps for VmoFileObject {
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

    fn get_vmo(
        &self,
        _file: &FileObject,
        _task: &Task,
        prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        let mut vmo =
            self.vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(impossible_error)?;
        if prot.contains(zx::VmarFlags::PERM_EXECUTE) {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        Ok(vmo)
    }
}
