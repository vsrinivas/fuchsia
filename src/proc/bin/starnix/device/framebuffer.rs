// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::framebuffer_server::{
    spawn_view_provider, FramebufferServer, IMAGE_HEIGHT, IMAGE_WIDTH,
};
use crate::device::DeviceOps;
use crate::fs::*;
use crate::lock::RwLock;
use crate::logging::*;
use crate::mm::MemoryAccessorExt;
use crate::syscalls::{SyscallResult, SUCCESS};
use crate::task::CurrentTask;
use crate::types::*;

use fuchsia_zircon as zx;
use std::sync::Arc;
use zerocopy::AsBytes;

pub struct Framebuffer {
    vmo: zx::Vmo,
    vmo_len: u32,
    info: RwLock<fb_var_screeninfo>,
    server: Option<Arc<FramebufferServer>>,
}

impl Framebuffer {
    pub fn new() -> Result<Arc<Self>, Errno> {
        let mut info = fb_var_screeninfo::default();

        // Hardcode a phone-sized screen with the pixel format Android expects
        info.xres = IMAGE_WIDTH;
        info.yres = IMAGE_HEIGHT;
        info.xres_virtual = info.xres;
        info.yres_virtual = info.yres;
        info.bits_per_pixel = 32;
        info.red = fb_bitfield { offset: 0, length: 8, msb_right: 0 };
        info.green = fb_bitfield { offset: 8, length: 8, msb_right: 0 };
        info.blue = fb_bitfield { offset: 16, length: 8, msb_right: 0 };
        info.transp = fb_bitfield { offset: 24, length: 8, msb_right: 0 };

        if let Ok(server) = FramebufferServer::new() {
            let server = Arc::new(server);
            let vmo = server.get_vmo()?;
            let vmo_len = vmo.info().map_err(|_| errno!(EINVAL))?.size_bytes as u32;
            // Fill the buffer with white pixels as a placeholder.
            if vmo.write(&vec![0xff; vmo_len as usize], 0).is_err() {
                tracing::warn!("could not write initial framebuffer");
            }

            Ok(Arc::new(Self { vmo, vmo_len, server: Some(server), info: RwLock::new(info) }))
        } else {
            let vmo_len = info.xres * info.yres * (info.bits_per_pixel / 8);
            let vmo = zx::Vmo::create(vmo_len as u64).map_err(|s| match s {
                zx::Status::NO_MEMORY => errno!(ENOMEM),
                _ => impossible_error(s),
            })?;
            Ok(Arc::new(Self { vmo, vmo_len, server: None, info: RwLock::new(info) }))
        }
    }

    /// Starts serving a view based on this framebuffer in `outgoing_dir`.
    pub fn start_server(
        &self,
        outgoing_dir: fidl::endpoints::ServerEnd<fidl_fuchsia_io::DirectoryMarker>,
    ) {
        if let Some(server) = &self.server {
            spawn_view_provider(server.clone(), outgoing_dir);
        }
    }
}

impl DeviceOps for Arc<Framebuffer> {
    fn open(
        &self,
        _current_task: &CurrentTask,
        dev: DeviceType,
        node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        if dev.minor() != 0 {
            return error!(ENODEV);
        }
        let mut info = node.info_write();
        info.size = self.vmo_len as usize;
        info.storage_size = self.vmo.get_size().map_err(impossible_error)? as usize;
        Ok(Box::new(Arc::clone(self)))
    }
}

impl FileOps for Arc<Framebuffer> {
    fileops_impl_seekable!();
    fileops_impl_nonblocking!();

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            FBIOGET_FSCREENINFO => {
                let info = self.info.read();
                let finfo = fb_fix_screeninfo {
                    id: unsafe {
                        std::mem::transmute::<[u8; 16], [i8; 16]>(*b"Starnix\0\0\0\0\0\0\0\0\0")
                    },
                    smem_start: 0,
                    smem_len: self.vmo_len,
                    type_: FB_TYPE_PACKED_PIXELS,
                    visual: FB_VISUAL_TRUECOLOR,
                    line_length: info.bits_per_pixel / 8 * info.xres,
                    ..fb_fix_screeninfo::default()
                };
                current_task.mm.write_object(UserRef::new(user_addr), &finfo)?;
                Ok(SUCCESS)
            }

            FBIOGET_VSCREENINFO => {
                let info = self.info.read();
                current_task.mm.write_object(UserRef::new(user_addr), &*info)?;
                Ok(SUCCESS)
            }

            FBIOPUT_VSCREENINFO => {
                let new_info: fb_var_screeninfo =
                    current_task.mm.read_object(UserRef::new(user_addr))?;
                let old_info = self.info.read();
                // We don't yet support actually changing anything
                if new_info.as_bytes() != old_info.as_bytes() {
                    return error!(EINVAL);
                }
                Ok(SUCCESS)
            }

            _ => {
                error!(EINVAL)
            }
        }
    }

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
