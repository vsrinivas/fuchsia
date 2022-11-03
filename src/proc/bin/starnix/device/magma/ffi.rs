// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased};
use magma::*;
use zerocopy::{AsBytes, FromBytes};

use crate::device::{
    magma::{
        file::{BufferInfo, ConnectionMap},
        magma::create_drm_image,
    },
    wayland::image_file::{ImageFile, ImageInfo},
};
use crate::fs::{Anon, FdFlags, VmoFileObject};
use crate::mm::{MemoryAccessor, MemoryAccessorExt};
use crate::task::CurrentTask;
use crate::types::*;

/// Returns a vector of at least one `T`. The vector will be of length `item_count` if > 0.
fn at_least_one<T>(item_count: usize) -> Vec<T>
where
    T: Default + Clone,
{
    vec![T::default(); std::cmp::max(1, item_count)]
}

/// Reads a sequence of objects starting at `addr`.
///
/// # Parameters
///   - `current_task`: The task from which to read the objects.
///   - `addr`: The address of the first item to read.
///   - `item_count`: The number of items to read. If 0, a 1-item vector will be returned to make
///                   sure that the calling code can safely pass `&mut vec[0]` to libmagma.
fn read_objects<T>(
    current_task: &CurrentTask,
    addr: UserAddress,
    item_count: usize,
) -> Result<Vec<T>, Errno>
where
    T: Default + Clone + FromBytes,
{
    let mut items = at_least_one::<T>(item_count);
    if item_count > 0 {
        let user_ref: UserRef<T> = addr.into();
        current_task.mm.read_objects(user_ref, &mut items)?;
    }
    Ok(items)
}

/// Creates a connection for a given device.
///
/// # Parameters
///   - `control`: The control struct containing the device to create a connection to.
///   - `response`: The struct that will be filled out to contain the response. This struct can be
///                 written back to userspace.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn create_connection(
    control: virtio_magma_create_connection2_ctrl,
    response: &mut virtio_magma_create_connection2_resp_t,
) {
    let mut connection_out: magma_connection_t = 0;
    response.result_return =
        unsafe { magma_create_connection2(control.device, &mut connection_out) as u64 };

    response.connection_out = connection_out;
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_CREATE_CONNECTION2 as u32;
}

/// Creates a DRM image VMO and imports it to magma.
///
/// Returns a `BufferInfo` containing the associated `BufferCollectionImportToken` and the magma
/// image info.
///
/// Upon successful completion, `response.image_out` will contain the handle to the magma buffer.
///
/// SAFETY: Makes an FFI call which takes ownership of a raw VMO handle. Invalid parameters are
/// dealt with by magma.
pub fn create_image(
    current_task: &CurrentTask,
    control: virtio_magma_virt_create_image_ctrl_t,
    response: &mut virtio_magma_virt_create_image_resp_t,
) -> Result<BufferInfo, Errno> {
    let create_info_address = UserAddress::from(control.create_info);
    let create_info_ptr = current_task.mm.read_object(UserRef::new(create_info_address))?;

    let create_info_address = UserAddress::from(create_info_ptr);
    let create_info = current_task.mm.read_object(UserRef::new(create_info_address))?;

    let (vmo, token, info) = create_drm_image(0, &create_info).map_err(|e| {
        tracing::warn!("Error creating drm image: {:?}", e);
        errno!(EINVAL)
    })?;

    let mut buffer_out = magma_buffer_t::default();
    response.result_return = unsafe {
        magma_import(control.connection as magma_connection_t, vmo.into_raw(), &mut buffer_out)
            as u64
    };

    response.image_out = buffer_out;
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_VIRT_CREATE_IMAGE as u32;

    Ok(BufferInfo::Image(ImageInfo { info, token }))
}

/// Imports a device to magma.
///
/// # Parameters
///   - `control`: The control struct containing the device channel to import from.
///   - `response`: The struct that will be filled out to contain the response. This struct can be
///                 written back to userspace.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn device_import(
    _control: virtio_magma_device_import_ctrl_t,
    response: &mut virtio_magma_device_import_resp_t,
) -> Result<zx::Channel, Errno> {
    let (client_channel, server_channel) = zx::Channel::create().map_err(|_| errno!(EINVAL))?;
    // TODO(fxbug.dev/100454): This currently picks the first available device, but multiple devices should
    // probably be exposed to clients.
    let entry = std::fs::read_dir("/dev/class/gpu")
        .map_err(|_| errno!(EINVAL))?
        .next()
        .ok_or_else(|| errno!(EINVAL))?
        .map_err(|_| errno!(EINVAL))?;

    let path = entry.path().into_os_string().into_string().map_err(|_| errno!(EINVAL))?;

    fdio::service_connect(&path, server_channel).map_err(|_| errno!(EINVAL))?;

    // TODO(fxbug.dev/12731): The device import should take ownership of the channel, at which point
    // this can be converted to `into_raw()`, and the return value of this function can be changed
    // to be `()`.
    let device_channel = client_channel.raw_handle();

    let mut device_out: u64 = 0;
    response.result_return =
        unsafe { magma_device_import(device_channel, &mut device_out as *mut u64) as u64 };

    response.device_out = device_out;
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_DEVICE_IMPORT as u32;

    Ok(client_channel)
}

/// Releases a magma device.
///
/// # Parameters
///  - `control`: The control message that contains the device to release.
///  - `response`: The response message that will be updated to write back to user space.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`. The FFI function is expected to
/// handle an invalid device id.
pub fn device_release(
    control: virtio_magma_device_release_ctrl_t,
    response: &mut virtio_magma_device_release_resp_t,
) {
    unsafe { magma_device_release(control.device) };
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_DEVICE_RELEASE as u32;
}

/// `WireDescriptor` matches the struct used by libmagma_linux to encode some fields of the magma
/// command descriptor.
#[repr(C)]
#[derive(FromBytes, AsBytes, Default, Debug)]
struct WireDescriptor {
    resource_count: u32,
    command_buffer_count: u32,
    wait_semaphore_count: u32,
    signal_semaphore_count: u32,
    flags: u64,
}

/// Executes a magma command.
///
/// This function bridges between the virtmagma structs and the magma structures. It also copies the
/// data into starnix in order to be able to pass pointers to the resources, command buffers, and
/// semaphore ids to magma.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn execute_command(
    current_task: &CurrentTask,
    control: virtio_magma_execute_command_ctrl_t,
    response: &mut virtio_magma_execute_command_resp_t,
) -> Result<(), Errno> {
    let virtmagma_command_descriptor_addr =
        UserRef::<virtmagma_command_descriptor>::new(control.descriptor.into());
    let command_descriptor = current_task.mm.read_object(virtmagma_command_descriptor_addr)?;

    // Read the virtmagma-internal struct that contains the counts and flags for the magma command
    // descriptor.
    let wire_descriptor: WireDescriptor =
        current_task.mm.read_object(UserAddress::from(command_descriptor.descriptor).into())?;

    // This is the command descriptor that will be populated from the virtmagma
    // descriptor and subsequently passed to magma_execute_command.
    let mut magma_command_descriptor = magma_command_descriptor {
        resource_count: wire_descriptor.resource_count,
        command_buffer_count: wire_descriptor.command_buffer_count,
        wait_semaphore_count: wire_descriptor.wait_semaphore_count,
        signal_semaphore_count: wire_descriptor.signal_semaphore_count,
        flags: wire_descriptor.flags,
        ..Default::default()
    };
    let semaphore_count =
        (wire_descriptor.wait_semaphore_count + wire_descriptor.signal_semaphore_count) as usize;

    // Read all the passed in resources, commands, and semaphore ids.
    let mut resources: Vec<magma_exec_resource> = read_objects(
        current_task,
        command_descriptor.resources.into(),
        wire_descriptor.resource_count as usize,
    )?;
    let mut command_buffers: Vec<magma_exec_command_buffer> = read_objects(
        current_task,
        command_descriptor.command_buffers.into(),
        wire_descriptor.command_buffer_count as usize,
    )?;
    let mut semaphores: Vec<u64> =
        read_objects(current_task, command_descriptor.semaphores.into(), semaphore_count)?;

    // Make sure the command descriptor contains valid pointers for the resources, command buffers,
    // and semaphore ids.
    magma_command_descriptor.resources = &mut resources[0] as *mut magma_exec_resource;
    magma_command_descriptor.command_buffers =
        &mut command_buffers[0] as *mut magma_exec_command_buffer;
    magma_command_descriptor.semaphore_ids = &mut semaphores[0] as *mut u64;

    response.result_return = unsafe {
        magma_execute_command(
            control.connection,
            control.context_id,
            &mut magma_command_descriptor as *mut magma_command_descriptor,
        ) as u64
    };

    Ok(())
}

/// Exports the provided magma buffer into a `zx::Vmo`, which is then wrapped in a file and added
/// to `current_task`'s files.
///
/// The file's `fd` is then written to the response object, which allows the client to interact with
/// the exported buffer.
///
/// # Parameters
///   - `current_task`: The task that is exporting the buffer.
///   - `control`: The control message that contains the buffer to export.
///   - `response`: The response message that will be updated to write back to user space.
///
/// Returns an error if adding the file to `current_task` fails.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`, and creates a `zx::Vmo` from
/// a raw handle provided by magma.
pub fn export_buffer(
    current_task: &CurrentTask,
    control: virtio_magma_export_ctrl_t,
    response: &mut virtio_magma_export_resp_t,
    connections: &ConnectionMap,
) -> Result<(), Errno> {
    let mut buffer_handle_out = 0;
    let status = unsafe {
        magma_export(
            control.connection as magma_connection_t,
            control.buffer as magma_buffer_t,
            &mut buffer_handle_out as *mut magma_handle_t,
        )
    };
    if status as u32 == MAGMA_STATUS_OK {
        let vmo = unsafe { zx::Vmo::from(zx::Handle::from_raw(buffer_handle_out)) };
        let file = match connections
            .get(&{ control.connection })
            .and_then(|buffers| buffers.get(&(control.buffer as magma_buffer_t)))
        {
            Some(BufferInfo::Image(image_info)) => {
                ImageFile::new_file(current_task, image_info.clone(), vmo)
            }
            _ => Anon::new_file(
                current_task,
                Box::new(VmoFileObject::new(Arc::new(vmo))),
                OpenFlags::RDWR,
            ),
        };
        let fd = current_task.files.add_with_flags(file, FdFlags::empty())?;
        response.buffer_handle_out = fd.raw() as u64;
    }

    response.result_return = status as u64;
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_EXPORT as u32;

    Ok(())
}

/// Calls flush on the provided `control.connection`.
///
/// SAFETY: Makes an FFI call to magma, which is expected to handle invalid connection parameters.
pub fn flush(control: virtio_magma_flush_ctrl_t, response: &mut virtio_magma_flush_resp_t) {
    response.result_return = unsafe { magma_flush(control.connection) as u64 };
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_FLUSH as u32;
}

/// Fetches a VMO handles from magma wraps it in a file, then adds that file to `current_task`.
///
/// # Parameters
///   - `current_task`: The task that the created file is added to, in `anon_fs`.
///   - `control`: The control message containing the image handle.
///   - `response`: The response which will contain the file descriptor for the created file.
///
/// SAFETY: Makes an FFI call to fetch a VMO handle. The VMO handle is expected to be valid if the
/// FFI call succeeds. Either way, creating a `zx::Vmo` with an invalid handle is safe.
pub fn get_buffer_handle(
    current_task: &CurrentTask,
    control: virtio_magma_get_buffer_handle2_ctrl_t,
    response: &mut virtio_magma_get_buffer_handle2_resp_t,
) -> Result<(), Errno> {
    let mut buffer_handle_out = 0;
    let status = unsafe {
        magma_get_buffer_handle2(
            control.buffer as magma_buffer_t,
            &mut buffer_handle_out as *mut magma_handle_t,
        )
    };

    if status != MAGMA_STATUS_OK as i32 {
        response.result_return = status as u64;
    } else {
        let vmo = unsafe { zx::Vmo::from(zx::Handle::from_raw(buffer_handle_out)) };
        let file = Anon::new_file(
            current_task,
            Box::new(VmoFileObject::new(Arc::new(vmo))),
            OpenFlags::RDWR,
        );
        let fd = current_task.files.add_with_flags(file, FdFlags::empty())?;
        response.handle_out = fd.raw() as u64;
        response.result_return = MAGMA_STATUS_OK as u64;
    }

    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_GET_BUFFER_HANDLE2 as u32;

    Ok(())
}

/// Writes the size of the provided buffer to `response`.
///
/// SAFETY: Makes an FFI call to magma. The buffer in `control` is expected to be valid, although
/// handling invalid buffer handles is left to magma.
pub fn get_buffer_size(
    control: virtio_magma_get_buffer_size_ctrl_t,
    response: &mut virtio_magma_get_buffer_size_resp_t,
) {
    response.result_return = unsafe { magma_get_buffer_size(control.buffer) };
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_GET_BUFFER_SIZE as u32;
}

/// Runs a magma query.
///
/// This function will create a new file in `current_task.files` if the magma query returns a VMO
/// handle. The file takes ownership of the VMO handle, and the file descriptor of the file is
/// returned to the client via `response`.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn query(
    current_task: &CurrentTask,
    control: virtio_magma_query_ctrl_t,
    response: &mut virtio_magma_query_resp_t,
) -> Result<(), Errno> {
    let mut result_buffer_out = 0;
    let mut result_out = 0;
    response.result_return = unsafe {
        magma_query(control.device, control.id, &mut result_buffer_out, &mut result_out) as u64
    };

    if result_buffer_out != zx::sys::ZX_HANDLE_INVALID {
        let vmo = unsafe { zx::Vmo::from(zx::Handle::from_raw(result_buffer_out)) };
        let file = Anon::new_file(
            current_task,
            Box::new(VmoFileObject::new(Arc::new(vmo))),
            OpenFlags::RDWR,
        );
        let fd = current_task.files.add_with_flags(file, FdFlags::empty())?;
        response.result_buffer_out = fd.raw() as u64;
    } else {
        response.result_buffer_out = u64::MAX;
    }

    response.result_out = result_out;

    Ok(())
}

/// Reads a notification from the connection channel and writes it to `control.buffer`.
///
/// Upon completion, `response.more_data_out` will be true if there is more data waiting to be read.
/// `response.buffer_size_out` contains the size of the returned buffer.
///
/// SAFETY: Makes an FFI call to magma with a buffer that is populated with data. The passed in
/// buffer pointer always points to a valid vector, even if the provided buffer length is 0.
pub fn read_notification_channel(
    current_task: &CurrentTask,
    control: virtio_magma_read_notification_channel2_ctrl_t,
    response: &mut virtio_magma_read_notification_channel2_resp_t,
) -> Result<(), Errno> {
    // Buffer has a min length of 1 to make sure the call to
    // `magma_read_notification_channel2` uses a valid reference.
    let mut buffer = vec![0; std::cmp::max(control.buffer_size as usize, 1)];
    let mut buffer_size_out = 0;
    let mut more_data_out: u8 = 0;

    response.result_return = unsafe {
        magma_read_notification_channel2(
            control.connection as magma_connection_t,
            &mut buffer[0] as *mut u8 as *mut std::ffi::c_void,
            control.buffer_size,
            &mut buffer_size_out,
            &mut more_data_out as *mut u8,
        ) as u64
    };

    response.more_data_out = more_data_out as u64;
    response.buffer_size_out = buffer_size_out;
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_READ_NOTIFICATION_CHANNEL2 as u32;

    current_task.mm.write_memory(UserAddress::from(control.buffer), &buffer)?;

    Ok(())
}

/// Releases the provided `control.buffer` in `control.connection`.
///
/// # Parameters:
///   - `control`: The control message containing the relevant buffer and connection.
///
/// SAFETY: The passed in `control` is expected to contain a valid buffer, otherwise the FFI call
/// will panic.
pub fn release_buffer(
    control: virtio_magma_release_buffer_ctrl_t,
    response: &mut virtio_magma_release_buffer_resp_t,
) {
    unsafe {
        magma_release_buffer(
            control.connection as magma_connection_t,
            control.buffer as magma_buffer_t,
        );
    }
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_RELEASE_BUFFER as u32;
}

/// Releases the provided `control.connection`.
///
/// # Parameters
///   - `control`: The control message that contains the connection to remove.
///   - `response`: The response message that will be updated to write back to user space.
///   - `connections`: The starnix-magma connection map, which is used to determine whether or not
///                    to call into magma to release the connection.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.
pub fn release_connection(
    control: virtio_magma_release_connection_ctrl_t,
    response: &mut virtio_magma_release_connection_resp_t,
    connections: &mut ConnectionMap,
) {
    let connection = control.connection as magma_connection_t;
    if connections.contains_key(&connection) {
        unsafe { magma_release_connection(connection) };
        connections.remove(&connection);
    }
    response.hdr.type_ = virtio_magma_ctrl_type_VIRTIO_MAGMA_RESP_RELEASE_CONNECTION as u32;
}
