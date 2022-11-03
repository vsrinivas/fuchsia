// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sysmem as fsysmem;
use fidl_fuchsia_sysmem::BufferCollectionTokenSynchronousProxy;
use fidl_fuchsia_ui_composition as fuicomp;
use fuchsia_component::client::connect_channel_to_protocol;
use fuchsia_image_format::*;
use fuchsia_scenic;
use fuchsia_zircon as zx;
use zerocopy::{AsBytes, FromBytes};

use std::sync::Arc;

use super::BufferCollectionFile;
use crate::fs::*;
use crate::mm::vmo::round_up_to_increment;
use crate::mm::MemoryAccessorExt;
use crate::syscalls::*;
use crate::types::*;

pub struct DmaBufNode {}
impl FsNodeOps for DmaBufNode {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        DmaBufFile::new_file()
    }
}

#[derive(AsBytes, FromBytes, Default)]
#[repr(packed)]
pub struct DmaBufHeader {
    pub type_: u32,
    pub fd: u32,
    pub flags: u32,
}

#[derive(AsBytes, FromBytes, Default)]
#[repr(packed)]
pub struct DmaBuf {
    pub width: u32,
    pub height: u32,
    pub format: u32,
    pub stride0: u32,
    pub stride1: u32,
    pub stride2: u32,
    pub offset0: u32,
    pub offset1: u32,
    pub offset2: u32,
}

#[derive(AsBytes, FromBytes, Default)]
#[repr(packed)]
pub struct DmaBufAllocationArgs {
    pub header: DmaBufHeader,
    pub buffer: DmaBuf,
}

#[derive(AsBytes, FromBytes, Default)]
#[repr(packed)]
pub struct DmaBufSyncArgs {
    pub header: DmaBufHeader,
    pub size: u32,
}

/// A file that is responsible for allocating and managing memory on behalf of wayland clients.
///
/// Clients send ioctls to this file to create DMA buffers, and the file communicates with scenic
/// and sysmem allocators to do so.
pub struct DmaBufFile {
    /// The sysmem allocator proxy.
    sysmem_proxy: fsysmem::AllocatorSynchronousProxy,

    /// The composition allocator proxy.
    composition_proxy: fuicomp::AllocatorSynchronousProxy,
}

impl DmaBufFile {
    /// Creates a new `DmaBufFile`.
    ///
    /// Returns an error if the file could not establish connections to `fsysmem::Allocator` and
    /// `fuicomp::Allocator`.
    pub fn new_file() -> Result<Box<dyn FileOps>, Errno> {
        let (server_end, client_end) = zx::Channel::create().map_err(|_| errno!(ENOENT))?;
        connect_channel_to_protocol::<fsysmem::AllocatorMarker>(server_end)
            .map_err(|_| errno!(ENOENT))?;
        let sysmem_proxy = fsysmem::AllocatorSynchronousProxy::new(client_end);

        let (server_end, client_end) = zx::Channel::create().map_err(|_| errno!(ENOENT))?;
        connect_channel_to_protocol::<fuicomp::AllocatorMarker>(server_end)
            .map_err(|_| errno!(ENOENT))?;
        let composition_proxy = fuicomp::AllocatorSynchronousProxy::new(client_end);

        Ok(Box::new(Self { sysmem_proxy, composition_proxy }))
    }

    /// Allocates a new buffer, maps it into the task's memory, and associates it with a file
    /// descriptor.
    fn allocate_dma_buffer(
        &self,
        current_task: &CurrentTask,
        buffer_allocation_args: &DmaBufAllocationArgs,
    ) -> Result<DmaBufAllocationArgs, Errno> {
        let buffer_collection_proxy = self.allocate_shared_collection()?;
        let buffer_collection_import_token =
            self.register_buffer_collection(&buffer_collection_proxy)?;
        let buffer_collection_info =
            self.bind_and_allocate_buffers(buffer_collection_proxy, buffer_allocation_args)?;

        let actual_image_constraints = buffer_collection_info.settings.image_format_constraints;
        let bytes_per_row = round_up_to_increment(
            actual_image_constraints.min_bytes_per_row as usize,
            actual_image_constraints.bytes_per_row_divisor as usize,
        )?;

        let fd = self.create_buffer_collection_file(
            current_task,
            buffer_collection_info,
            buffer_collection_import_token,
        )?;

        let mut response = DmaBufAllocationArgs::default();
        // Matches VIRTIO_WL_RESP_VFD_NEW_DMABUF in //zircon/system/ulib/virtio/include/virtio/wl.h.
        const NEW_DMABUF: u32 = 0x1002;
        response.header.type_ = NEW_DMABUF;
        response.header.fd = fd.raw() as u32;
        response.buffer.width = buffer_allocation_args.buffer.width;
        response.buffer.height = buffer_allocation_args.buffer.height;
        response.buffer.stride0 = bytes_per_row as u32;

        Ok(response)
    }

    /// Allocates a shared collection with `fsysmem::Allocator`, and returns a token proxy for the
    /// shared collection.
    fn allocate_shared_collection(
        &self,
    ) -> Result<fsysmem::BufferCollectionTokenSynchronousProxy, Errno> {
        let (collection_token, remote) =
            fidl::endpoints::create_endpoints::<fsysmem::BufferCollectionTokenMarker>()
                .map_err(|_| errno!(EINVAL))?;
        let buffer_collection_token_proxy =
            fsysmem::BufferCollectionTokenSynchronousProxy::new(collection_token.into_channel());

        self.sysmem_proxy.allocate_shared_collection(remote).map_err(|_| errno!(EINVAL))?;

        Ok(buffer_collection_token_proxy)
    }

    /// Registers the buffer collection with `fuicomp::Allocator`.
    ///
    /// The provided `buffer_collection_proxy` is duplicated and synced before the collection is
    /// registered.
    fn register_buffer_collection(
        &self,
        buffer_collection_proxy: &BufferCollectionTokenSynchronousProxy,
    ) -> Result<fuicomp::BufferCollectionImportToken, Errno> {
        let (scenic_token, remote) =
            fidl::endpoints::create_endpoints::<fsysmem::BufferCollectionTokenMarker>()
                .map_err(|_| errno!(EINVAL))?;

        buffer_collection_proxy.duplicate(u32::MAX, remote).map_err(|_| errno!(EINVAL))?;
        buffer_collection_proxy.sync(zx::Time::INFINITE).map_err(|_| errno!(EINVAL))?;

        let fuchsia_scenic::BufferCollectionTokenPair { export_token, import_token } =
            fuchsia_scenic::BufferCollectionTokenPair::new();

        let args = fuicomp::RegisterBufferCollectionArgs {
            export_token: Some(export_token),
            buffer_collection_token: Some(scenic_token),
            ..fuicomp::RegisterBufferCollectionArgs::EMPTY
        };

        self.composition_proxy
            .register_buffer_collection(args, zx::Time::INFINITE)
            .map_err(|_| errno!(EINVAL))?
            // All register buffer collection errors just map to EINVAL.
            .map_err(|_| errno!(EINVAL))?;

        Ok(import_token)
    }

    /// Binds the shared collection represented by `buffer_collection_proxy`.
    ///
    /// The buffer collection has its name and constraints set, and then the method waits for the
    /// buffers to be allocated.
    ///
    /// Once the buffers have been allocated, the buffer collection proxy is closed, and the method
    /// returns the buffer collection info associated with the collection.
    fn bind_and_allocate_buffers(
        &self,
        buffer_collection_proxy: BufferCollectionTokenSynchronousProxy,
        buffer_allocation_args: &DmaBufAllocationArgs,
    ) -> Result<fsysmem::BufferCollectionInfo2, Errno> {
        let (buffer_token, remote) =
            fidl::endpoints::create_endpoints::<fsysmem::BufferCollectionMarker>()
                .map_err(|_| errno!(EINVAL))?;
        self.sysmem_proxy
            .bind_shared_collection(buffer_collection_proxy.into_channel().into(), remote)
            .map_err(|_| errno!(EINVAL))?;

        let buffer_collection =
            fsysmem::BufferCollectionSynchronousProxy::new(buffer_token.into_channel());
        const VMO_NAME: &str = "Starnix-DMABuf";
        const NAME_PRIORITY: u32 = 8;
        buffer_collection.set_name(NAME_PRIORITY, VMO_NAME).map_err(|_| errno!(EINVAL))?;

        let mut constraints = buffer_collection_constraints(&buffer_allocation_args.buffer);
        buffer_collection.set_constraints(true, &mut constraints).map_err(|_| errno!(EINVAL))?;

        let (_status, buffer_collection_info) = buffer_collection
            .wait_for_buffers_allocated(zx::Time::INFINITE)
            .map_err(|_| errno!(EINVAL))?;

        let _ = buffer_collection.close();

        Ok(buffer_collection_info)
    }

    /// Creates a file that stores the first vmo in the buffer collection.
    ///
    /// The file also stores the buffer collection import token, which is passed to the wayland
    /// bridge when the client passes this file descriptor in a socket message.
    ///
    /// Returns the `FdNumber` of the created file.
    fn create_buffer_collection_file(
        &self,
        current_task: &CurrentTask,
        mut buffer_collection_info: fsysmem::BufferCollectionInfo2,
        buffer_collection_import_token: fuicomp::BufferCollectionImportToken,
    ) -> Result<FdNumber, Errno> {
        let vmo =
            Arc::new(buffer_collection_info.buffers[0].vmo.take().ok_or_else(|| errno!(EINVAL))?);
        current_task.files.add_with_flags(
            BufferCollectionFile::new_file(current_task, buffer_collection_import_token, vmo)?,
            FdFlags::empty(),
        )
    }
}

impl FileOps for DmaBufFile {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        _request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        // TODO: Don't assume that this is a NEW_DMA_BUF request.
        // There are some macros for parsing request in the wayland demo that will need to be
        // matched here.
        let allocation_args = current_task.mm.read_object(UserRef::new(user_addr))?;
        let result = self.allocate_dma_buffer(current_task, &allocation_args)?;
        current_task.mm.write_object(UserRef::new(user_addr), &result)?;

        Ok(SUCCESS)
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EINVAL)
    }
}

/// Returns the buffer collection constraints to be set on the buffer collection associated with
/// `buffer`.
pub fn buffer_collection_constraints(buffer: &DmaBuf) -> fsysmem::BufferCollectionConstraints {
    let usage = fsysmem::BufferUsage {
        cpu: fsysmem::CPU_USAGE_READ_OFTEN | fsysmem::CPU_USAGE_WRITE_OFTEN,
        ..BUFFER_USAGE_DEFAULT
    };

    let buffer_memory_constraints = fsysmem::BufferMemoryConstraints {
        ram_domain_supported: true,
        cpu_domain_supported: true,
        ..BUFFER_MEMORY_CONSTRAINTS_DEFAULT
    };

    let pixel_format = fsysmem::PixelFormat {
        type_: drm_format_to_sysmem_format(buffer.format)
            .unwrap_or(fsysmem::PixelFormatType::Invalid),
        has_format_modifier: true,
        format_modifier: fsysmem::FormatModifier { value: fsysmem::FORMAT_MODIFIER_LINEAR },
    };

    let mut image_constraints = fsysmem::ImageFormatConstraints {
        min_coded_width: buffer.width,
        min_coded_height: buffer.height,
        max_coded_width: buffer.width,
        max_coded_height: buffer.height,
        min_bytes_per_row: min_bytes_per_row(buffer.format, buffer.width).unwrap_or(0),
        color_spaces_count: 1,
        pixel_format,
        ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
    };
    image_constraints.color_space[0].type_ = fsysmem::ColorSpaceType::Srgb;

    let mut constraints = fsysmem::BufferCollectionConstraints {
        min_buffer_count: 1,
        usage,
        has_buffer_memory_constraints: true,
        buffer_memory_constraints,
        image_format_constraints_count: 1,
        ..BUFFER_COLLECTION_CONSTRAINTS_DEFAULT
    };
    constraints.image_format_constraints[0] = image_constraints;

    constraints
}
