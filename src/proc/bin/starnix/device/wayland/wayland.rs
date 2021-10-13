// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::wayland::DmaNode;
use crate::fs::socket::*;
use crate::fs::FsString;
use crate::fs::*;
use crate::mode;
use crate::task::{Kernel, Task};
use crate::types::*;

use std::sync::{Arc, Weak};

pub struct Wayland {
    /// The display socket which the wayland compositor is listening to.
    #[allow(dead_code)]
    display_socket: SocketHandle,

    /// The kernel in which the wayland compositor is running.
    #[allow(dead_code)]
    kernel: Weak<Kernel>,
}

impl Wayland {
    /// Creates a new wayland compositor instance.
    ///
    /// # Parameters
    /// - `display_path`: The absolute path at which to create the display socket.
    /// - `device_path`: The absolute path at which to create the wayland device file, which
    ///                  allocates memory on behalf of the wayland client.
    pub fn new(display_path: FsString, device_path: FsString, task: &Task) -> Result<Self, Errno> {
        let display_socket = Wayland::create_display_socket(task, display_path)?;
        Wayland::create_device_file(task, device_path)?;
        Ok(Wayland { display_socket, kernel: Arc::downgrade(&task.thread_group.kernel) })
    }

    /// Creates a wayland display socket at the provided path, using `task` to resolve the path.
    fn create_display_socket(task: &Task, display_path: FsString) -> Result<SocketHandle, Errno> {
        let display_socket = Socket::new(SocketDomain::Unix, SocketType::Stream);

        let (socket_parent, socket_basename) =
            task.lookup_parent_at(FdNumber::AT_FDCWD, &display_path)?;

        let mode = task.fs.apply_umask(mode!(IFSOCK, 0o765));
        let _socket_dir_entry = socket_parent.entry.bind_socket(
            socket_basename,
            display_socket.clone(),
            SocketAddress::Unix(display_path.clone()),
            mode,
        );
        display_socket.lock().listen(1)?;

        Ok(display_socket)
    }

    /// Creates a memory allocation device file at the provided path.
    fn create_device_file(task: &Task, device_path: FsString) -> Result<(), Errno> {
        let (device_parent, device_basename) =
            task.lookup_parent_at(FdNumber::AT_FDCWD, &device_path)?;
        let mode = task.fs.apply_umask(mode!(IFREG, 0o765));
        let _device_entry = device_parent.entry.add_node_ops(device_basename, mode, DmaNode {})?;
        Ok(())
    }
}
