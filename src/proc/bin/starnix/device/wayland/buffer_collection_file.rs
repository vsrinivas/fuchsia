// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_composition as fuicomp;
use fuchsia_zircon as zx;

use std::sync::Arc;

use crate::errno;
use crate::error;
use crate::fd_impl_seekable;
use crate::fs::*;
use crate::task::{Kernel, Task};
use crate::types::*;

pub struct BufferCollectionFile {
    /// The import token for the buffer collection this file represents. This is used by, for
    /// example, the wayland compositor to pass to the wayland bridge.
    pub token: fuicomp::BufferCollectionImportToken,

    /// The Vmo that backs this file.
    pub vmo: Arc<zx::Vmo>,
}

impl BufferCollectionFile {
    /// Creates a new anonymous `TimerFile` in `kernel`.
    ///
    /// Returns an error if the `zx::Timer` could not be created.
    pub fn new(
        kernel: &Kernel,
        token: fuicomp::BufferCollectionImportToken,
        vmo: Arc<zx::Vmo>,
    ) -> Result<FileHandle, Errno> {
        Ok(Anon::new_file(
            anon_fs(kernel),
            Box::new(BufferCollectionFile { token, vmo }),
            OpenFlags::RDWR,
        ))
    }
}

impl FileOps for BufferCollectionFile {
    fd_impl_seekable!();

    fn read_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        VmoFileObject::read_at(&self.vmo, file, task, offset, data)
    }

    fn write_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        VmoFileObject::write_at(&self.vmo, file, task, offset, data)
    }
}
