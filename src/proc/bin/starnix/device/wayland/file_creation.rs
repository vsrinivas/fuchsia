// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::magma_file::MagmaNode;
use crate::device::wayland::DmaBufNode;
use crate::fs::socket::*;
use crate::fs::FdNumber;
use crate::fs::FsString;
use crate::mode;
use crate::task::CurrentTask;
use crate::types::*;

/// Creates a wayland display socket at the provided path, using `current_task` to resolve the path.
pub fn create_display_socket(
    current_task: &CurrentTask,
    display_path: FsString,
) -> Result<SocketHandle, Errno> {
    let display_socket = Socket::new(SocketDomain::Unix, SocketType::Stream);

    let (socket_parent, socket_basename) =
        current_task.lookup_parent_at(FdNumber::AT_FDCWD, &display_path)?;

    let mode = current_task.fs.apply_umask(mode!(IFSOCK, 0o765));
    let _socket_dir_entry = socket_parent.entry.bind_socket(
        socket_basename,
        display_socket.clone(),
        SocketAddress::Unix(display_path.clone()),
        mode,
    );
    display_socket.listen(1)?;

    Ok(display_socket)
}

/// Creates a memory allocation device file at the provided path.
pub fn create_device_file(current_task: &CurrentTask, device_path: FsString) -> Result<(), Errno> {
    let (device_parent, device_basename) =
        current_task.lookup_parent_at(FdNumber::AT_FDCWD, &device_path)?;
    let mode = current_task.fs.apply_umask(mode!(IFREG, 0o765));
    let _device_entry = device_parent.entry.add_node_ops(device_basename, mode, DmaBufNode {})?;
    Ok(())
}

pub fn create_magma_file(task: &CurrentTask, device_path: FsString) -> Result<(), Errno> {
    let (device_parent, device_basename) =
        task.lookup_parent_at(FdNumber::AT_FDCWD, &device_path)?;
    let mode = task.fs.apply_umask(mode!(IFREG, 0o765));
    let _device_entry = device_parent.entry.add_node_ops(device_basename, mode, MagmaNode {})?;
    Ok(())
}
