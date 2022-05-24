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
    wayland::image_file::ImageInfo,
};
use crate::fs::{anon_fs, Anon, FdFlags, VmoFileObject};
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
        .ok_or(errno!(EINVAL))?
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

    Ok(client_channel)
}

/// Releases a magma device.
///
/// # Parameters
///  - `control`: The control message that contains the device to release.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`. The FFI function is expected to
/// handle an invalid device id.
pub fn device_release(control: virtio_magma_device_release_ctrl_t) {
    unsafe { magma_device_release(control.device) };
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
    let mut magma_command_descriptor = magma_command_descriptor::default();
    magma_command_descriptor.resource_count = wire_descriptor.resource_count;
    magma_command_descriptor.command_buffer_count = wire_descriptor.command_buffer_count;
    magma_command_descriptor.wait_semaphore_count = wire_descriptor.wait_semaphore_count;
    magma_command_descriptor.signal_semaphore_count = wire_descriptor.signal_semaphore_count;
    magma_command_descriptor.flags = wire_descriptor.flags;
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

/// Writes the size of the provided buffer to `response`.
///
/// SAFETY: Makes an FFI call to magma. The buffer in `control` is expected to be valid, although
/// handling invalid buffer handles is left to magma.
pub fn get_buffer_size(
    control: virtio_magma_get_buffer_size_ctrl_t,
    response: &mut virtio_magma_get_buffer_size_resp_t,
) {
    response.result_return = unsafe { magma_get_buffer_size(control.buffer) };
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
            anon_fs(current_task.kernel()),
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

/// Releases the provided `control.connection`.
///
/// # Parameters
///   - `control`: The control message that contains the connection to remove.
///   - `connections`: The starnix-magma connection map, which is used to determine whether or not
///                    to call into magma to release the connection.
///
/// SAFETY: Makes an FFI call to populate the fields of `response`.

pub fn release_connection(
    control: virtio_magma_release_connection_ctrl_t,
    connections: &mut ConnectionMap,
) {
    let connection = control.connection as magma_connection_t;
    if connections.contains_key(&connection) {
        unsafe { magma_release_connection(connection) };
        connections.remove(&connection);
    }
}
