// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_ui_composition as fuicomp;
use fuchsia_zircon as zx;
use fuchsia_zircon::{AsHandleRef, HandleBased};
use magma::*;

use std::sync::Arc;

use crate::fs::*;
use crate::task::CurrentTask;
use crate::types::*;

pub struct ImageInfo {
    /// The magma image info associated with the `vmo`.
    pub info: magma_image_info_t,

    /// The `BufferCollectionImportToken` associated with this file.
    pub token: Option<fuicomp::BufferCollectionImportToken>,
}

impl Clone for ImageInfo {
    fn clone(&self) -> Self {
        ImageInfo {
            info: self.info,
            token: self.token.as_ref().map(|token| fuicomp::BufferCollectionImportToken {
                value: fidl::EventPair::from_handle(
                    token
                        .value
                        .as_handle_ref()
                        .duplicate(zx::Rights::SAME_RIGHTS)
                        .expect("Failed to duplicate the buffer token."),
                ),
            }),
        }
    }
}

pub struct ImageFile {
    pub info: ImageInfo,

    pub vmo: Arc<zx::Vmo>,
}

impl ImageFile {
    pub fn new_file(current_task: &CurrentTask, info: ImageInfo, vmo: zx::Vmo) -> FileHandle {
        Anon::new_file(
            current_task,
            Box::new(ImageFile { info, vmo: Arc::new(vmo) }),
            OpenFlags::RDWR,
        )
    }
}

impl FileOps for ImageFile {
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
}
