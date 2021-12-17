// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_upper_case_globals)]

use fuchsia_zircon as zx;
use fuchsia_zircon::{AsHandleRef, HandleBased};
use magma::*;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;

use super::magma::*;
use crate::device::wayland::image_file::*;
use crate::errno;
use crate::error;
use crate::fd_impl_nonblocking;
use crate::fd_impl_nonseekable;
use crate::fs::*;
use crate::logging::impossible_error;
use crate::syscalls::*;
use crate::task::{CurrentTask, EventHandler, Waiter};
use crate::types::*;

pub struct MagmaNode {}
impl FsNodeOps for MagmaNode {
    fn open(&self, _node: &FsNode, _flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        MagmaFile::new()
    }
}

pub struct MagmaFile {
    channel: Arc<Mutex<Option<zx::Channel>>>,

    infos: Arc<Mutex<HashMap<magma_buffer_t, ImageInfo>>>,
}

impl MagmaFile {
    pub fn new() -> Result<Box<dyn FileOps>, Errno> {
        Ok(Box::new(Self {
            channel: Arc::new(Mutex::new(None)),
            infos: Arc::new(Mutex::new(HashMap::new())),
        }))
    }
}

impl FileOps for MagmaFile {
    fd_impl_nonseekable!();
    fd_impl_nonblocking!();

    fn ioctl(
        &self,
        _file: &FileObject,
        task: &CurrentTask,
        _request: u32,
        in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        let (command, command_type) = read_magma_command_and_type(task, in_addr)?;
        let response_address = UserAddress::from(command.response_address);

        match command_type {
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_DEVICE_IMPORT => {
                let (_control, mut response): (
                    virtio_magma_device_import_ctrl_t,
                    virtio_magma_device_import_resp_t,
                ) = read_control_and_response(task, &command)?;

                let (client_channel, server_channel) =
                    zx::Channel::create().map_err(|_| errno!(EINVAL))?;
                fdio::service_connect(&"/dev/class/gpu/000", server_channel)
                    .map_err(|_| errno!(EINVAL))?;

                let device_channel = client_channel.raw_handle();
                *self.channel.lock() = Some(client_channel);

                let mut device_out: usize = 0;
                response.result_return = unsafe {
                    magma_device_import(device_channel, &mut device_out as *mut usize) as u64
                };

                response.device_out = device_out;
                response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_DEVICE_IMPORT as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_CREATE_CONNECTION2 => {
                let (control, mut response): (
                    virtio_magma_create_connection2_ctrl,
                    virtio_magma_create_connection2_resp_t,
                ) = read_control_and_response(task, &command)?;

                let mut connection_out: magma_connection_t = std::ptr::null_mut();
                response.result_return = unsafe {
                    magma_create_connection2(control.device as usize, &mut connection_out) as u64
                };

                response.connection_out = connection_out as usize;
                response.hdr.type_ =
                    virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_CREATE_CONNECTION2 as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_DEVICE_RELEASE => {
                let (control, mut response): (
                    virtio_magma_device_release_ctrl_t,
                    virtio_magma_device_release_resp_t,
                ) = read_control_and_response(task, &command)?;

                unsafe { magma_device_release(control.device as usize) };

                response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_DEVICE_RELEASE as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_VIRT_CREATE_IMAGE => {
                let (control, mut response): (
                    virtio_magma_virt_create_image_ctrl_t,
                    virtio_magma_virt_create_image_resp_t,
                ) = read_control_and_response(task, &command)?;

                let mut create_info_ptr: u64 = 0;
                let create_info_address = UserAddress::from(control.create_info as u64);
                task.mm.read_object(UserRef::new(create_info_address), &mut create_info_ptr)?;

                let mut create_info = magma_image_create_info_t::default();
                let create_info_address = UserAddress::from(create_info_ptr);
                task.mm.read_object(UserRef::new(create_info_address), &mut create_info)?;

                let (vmo, token, info) =
                    create_drm_image(0, &create_info).map_err(|_| errno!(EINVAL))?;
                let vmo = Arc::new(vmo);
                let vmo_dup =
                    vmo.duplicate_handle(zx::Rights::SAME_RIGHTS).map_err(impossible_error)?;

                let mut buffer_out = magma_buffer_t::default();
                response.result_return = unsafe {
                    magma_import(
                        control.connection as magma_connection_t,
                        vmo_dup.into_raw(),
                        &mut buffer_out,
                    ) as u64
                };
                self.infos.lock().insert(buffer_out, ImageInfo { info, token, vmo });

                response.image_out = buffer_out;
                response.hdr.type_ =
                    virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_VIRT_CREATE_IMAGE as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_VIRT_GET_IMAGE_INFO => {
                let (control, mut response): (
                    virtio_magma_virt_get_image_info_ctrl_t,
                    virtio_magma_virt_get_image_info_resp_t,
                ) = read_control_and_response(task, &command)?;

                // TODO(fxb/90145): Store images per connection.
                let _connection = control.connection;
                let image = control.image as usize;

                let mut image_info_ptr: u64 = 0;
                let image_info_address = UserAddress::from(control.image_info_out as u64);
                task.mm.read_object(UserRef::new(image_info_address), &mut image_info_ptr)?;

                let infos = self.infos.lock();
                let image_info = infos.get(&image).ok_or(errno!(EINVAL))?;
                let image_info_out = UserAddress::from(image_info_ptr as u64);
                task.mm.write_object(UserRef::new(image_info_out), &image_info.info)?;

                response.hdr.type_ =
                    virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_VIRT_GET_IMAGE_INFO as u32;
                response.result_return = MAGMA_STATUS_OK as u64;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_GET_BUFFER_SIZE => {
                let (control, mut response): (
                    virtio_magma_get_buffer_size_ctrl_t,
                    virtio_magma_get_buffer_size_resp_t,
                ) = read_control_and_response(task, &command)?;

                response.result_return = unsafe { magma_get_buffer_size(control.buffer as usize) };
                response.hdr.type_ =
                    virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_GET_BUFFER_SIZE as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_GET_BUFFER_HANDLE2 => {
                let (control, mut response): (
                    virtio_magma_get_buffer_handle2_ctrl_t,
                    virtio_magma_get_buffer_handle2_resp_t,
                ) = read_control_and_response(task, &command)?;

                let infos = self.infos.lock();
                // Note that this only stores handles for images. Once other buffer types are
                // supported this will need to be updated if they aren't stored in the same
                // collection.
                let image_info = infos.get(&(control.buffer as usize)).ok_or(errno!(EINVAL))?;
                let file = ImageFile::new(task.kernel(), image_info.clone());
                let fd = task.files.add_with_flags(file, FdFlags::empty())?;

                response.handle_out = fd.raw() as usize;
                response.result_return = MAGMA_STATUS_OK as u64;
                response.hdr.type_ =
                    virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_GET_BUFFER_HANDLE2 as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_EXPORT => {
                let (control, mut response): (
                    virtio_magma_export_ctrl_t,
                    virtio_magma_export_resp_t,
                ) = read_control_and_response(task, &command)?;

                let infos = self.infos.lock();
                let image_info = infos.get(&(control.buffer as usize)).ok_or(errno!(EINVAL))?;
                let file = ImageFile::new(task.kernel(), image_info.clone());
                let fd = task.files.add_with_flags(file, FdFlags::empty())?;

                response.buffer_handle_out = fd.raw() as usize;
                response.result_return = MAGMA_STATUS_OK as u64;
                response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_EXPORT as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            virtio_magma_ctrl_type_VIRTIO_MAGMA_CMD_IMPORT => {
                let (control, mut response): (
                    virtio_magma_import_ctrl_t,
                    virtio_magma_import_resp_t,
                ) = read_control_and_response(task, &command)?;

                let file = task.files.get(FdNumber::from_raw(control.buffer_handle as i32))?;
                let file = file.downcast_file::<ImageFile>().ok_or(errno!(EINVAL))?;
                let vmo = file
                    .info
                    .vmo
                    .duplicate_handle(zx::Rights::SAME_RIGHTS)
                    .map_err(impossible_error)?;

                let mut buffer_out = magma_buffer_t::default();
                response.result_return = unsafe {
                    magma_import(
                        control.connection as magma_connection_t,
                        vmo.into_raw(),
                        &mut buffer_out,
                    ) as u64
                };

                let _ = task.files.close(FdNumber::from_raw(control.buffer_handle as i32));
                self.infos.lock().insert(buffer_out, file.info.clone());

                response.buffer_out = buffer_out;
                response.result_return = MAGMA_STATUS_OK as u64;
                response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_IMPORT as u32;
                task.mm.write_object(UserRef::new(response_address), &response)
            }
            t => {
                log::warn!("Got unknown request: {:?}", t);
                error!(ENOSYS)
            }
        }?;

        Ok(SUCCESS)
    }

    fn read(
        &self,
        _file: &FileObject,
        _task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn write(
        &self,
        _file: &FileObject,
        _task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}
