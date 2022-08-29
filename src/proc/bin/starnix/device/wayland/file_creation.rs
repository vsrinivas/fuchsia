// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::wayland::DmaBufNode;
use crate::fs::socket::*;
use crate::fs::FdNumber;
use crate::fs::FsString;
use crate::task::CurrentTask;
use crate::types::*;

/// Creates a wayland display socket at the provided path, using `current_task` to resolve the path.
pub fn create_display_socket(
    current_task: &CurrentTask,
    display_path: FsString,
) -> Result<SocketHandle, Errno> {
    let display_socket =
        Socket::new(SocketDomain::Unix, SocketType::Stream, SocketProtocol::default());

    let (socket_parent, socket_basename) =
        current_task.lookup_parent_at(FdNumber::AT_FDCWD, &display_path)?;

    let _socket_dir_entry = socket_parent.entry.bind_socket(
        current_task,
        socket_basename,
        display_socket.clone(),
        SocketAddress::Unix(display_path.clone()),
        current_task.fs().apply_umask(mode!(IFSOCK, 0o777)),
        current_task.as_fscred(),
    );
    display_socket.listen(1, current_task.as_ucred())?;

    Ok(display_socket)
}

/// Creates a memory allocation device file at the provided path.
pub fn create_device_file(current_task: &CurrentTask, device_path: FsString) -> Result<(), Errno> {
    let (device_parent, device_basename) =
        current_task.lookup_parent_at(FdNumber::AT_FDCWD, &device_path)?;
    let mode = current_task.fs().apply_umask(mode!(IFREG, 0o777));
    let _device_entry = device_parent.entry.add_node_ops(device_basename, mode, DmaBufNode {})?;
    Ok(())
}
