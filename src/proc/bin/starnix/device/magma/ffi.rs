// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef};
use magma::*;

use crate::types::*;

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
