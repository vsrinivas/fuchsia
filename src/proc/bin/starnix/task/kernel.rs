// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef};
use once_cell::sync::OnceCell;
use parking_lot::{Mutex, RwLock};
use std::ffi::CStr;
use std::sync::Arc;

use crate::device::{DeviceMode, DeviceRegistry};
use crate::fs::{FileOps, FileSystemHandle, FsNode};
use crate::task::*;
use crate::types::{DeviceType, Errno, OpenFlags};

#[cfg(test)]
use std::ffi::CString;

pub struct Kernel {
    /// The Zircon job object that holds the processes running in this kernel.
    pub job: zx::Job,

    /// The processes and threads running in this kernel, organized by pid_t.
    pub pids: RwLock<PidTable>,

    /// The default namespace for abstract AF_UNIX sockets in this kernel.
    ///
    /// Rather than use this default namespace, abstract socket addresses
    /// should be looked up in the AbstractSocketNamespace on each Task
    /// object because some Task objects might have a non-default namespace.
    pub default_abstract_socket_namespace: Arc<AbstractSocketNamespace>,

    /// The kernel command line. Shows up in /proc/cmdline.
    pub cmdline: Vec<u8>,

    // Owned by anon_node.rs
    pub anon_fs: OnceCell<FileSystemHandle>,
    // Owned by pipe.rs
    pub pipe_fs: OnceCell<FileSystemHandle>,
    /// Owned by socket.rs
    pub socket_fs: OnceCell<FileSystemHandle>,
    // Owned by devtmpfs.rs
    pub dev_tmp_fs: OnceCell<FileSystemHandle>,
    // Owned by procfs.rs
    pub proc_fs: OnceCell<FileSystemHandle>,
    // Owned by sysfs.rs
    pub sys_fs: OnceCell<FileSystemHandle>,
    // Owned by selinux.rs
    pub selinux_fs: OnceCell<FileSystemHandle>,

    /// The registry of device drivers.
    pub device_registry: RwLock<DeviceRegistry>,

    /// The outgoing directory for the component that is being run. This is used to serve a
    /// `ViewProvider` on behalf of the component, if the component displays graphics.
    ///
    /// Note: This assumes there is only one component running in the Kernel.
    pub outgoing_dir: Mutex<Option<fidl::Channel>>,
}

impl Kernel {
    pub fn new(name: &CStr) -> Result<Kernel, zx::Status> {
        let job = fuchsia_runtime::job_default().create_child_job()?;
        job.set_name(&name)?;

        Ok(Kernel {
            job,
            pids: RwLock::new(PidTable::new()),
            default_abstract_socket_namespace: AbstractSocketNamespace::new(),
            cmdline: Vec::new(),
            anon_fs: OnceCell::new(),
            pipe_fs: OnceCell::new(),
            dev_tmp_fs: OnceCell::new(),
            proc_fs: OnceCell::new(),
            socket_fs: OnceCell::new(),
            sys_fs: OnceCell::new(),
            selinux_fs: OnceCell::new(),
            device_registry: RwLock::new(DeviceRegistry::new_with_common_devices()),
            outgoing_dir: Mutex::new(None),
        })
    }

    #[cfg(test)]
    pub fn new_for_testing() -> Arc<Kernel> {
        Arc::new(
            Self::new(&CString::new("testing").unwrap()).expect("Failed to create test kernel."),
        )
    }

    /// Opens a device file (driver) identified by `dev`.
    pub fn open_device(
        &self,
        node: &FsNode,
        flags: OpenFlags,
        dev: DeviceType,
        mode: DeviceMode,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let registry = self.device_registry.read();
        registry.open_device(node, flags, dev, mode)
    }
}
