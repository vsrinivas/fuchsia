// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, Process};
use once_cell::sync::OnceCell;
use std::collections::{BTreeMap, HashSet};
use std::ffi::CStr;
use std::iter::FromIterator;
use std::sync::Arc;

use crate::device::framebuffer::Framebuffer;
use crate::device::{BinderDriver, DeviceMode, DeviceRegistry};
use crate::fs::socket::SocketAddress;
use crate::fs::{FileOps, FileSystemHandle, FsNode};
use crate::lock::RwLock;
use crate::task::*;
use crate::types::{DeviceType, Errno, OpenFlags};

pub struct Kernel {
    /// The Zircon job object that holds the processes running in this kernel.
    pub job: zx::Job,

    /// The main starnix process. This process is used to create new processes when using the
    /// restricted executor.
    pub starnix_process: Process,

    /// The processes and threads running in this kernel, organized by pid_t.
    pub pids: RwLock<PidTable>,

    /// The default namespace for abstract AF_UNIX sockets in this kernel.
    ///
    /// Rather than use this default namespace, abstract socket addresses
    /// should be looked up in the AbstractSocketNamespace on each Task
    /// object because some Task objects might have a non-default namespace.
    pub default_abstract_socket_namespace: Arc<AbstractUnixSocketNamespace>,

    /// The default namespace for abstract AF_VSOCK sockets in this kernel.
    pub default_abstract_vsock_namespace: Arc<AbstractVsockSocketNamespace>,

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
    // Owned by devpts.rs
    pub dev_pts_fs: OnceCell<FileSystemHandle>,
    // Owned by procfs.rs
    pub proc_fs: OnceCell<FileSystemHandle>,
    // Owned by sysfs.rs
    pub sys_fs: OnceCell<FileSystemHandle>,
    // Owned by selinux.rs
    pub selinux_fs: OnceCell<FileSystemHandle>,

    /// The registry of device drivers.
    pub device_registry: RwLock<DeviceRegistry>,

    // The features enabled for the galaxy this kernel is associated with, as specified in
    // the galaxy's configuration file.
    pub features: HashSet<String>,

    /// A `Framebuffer` that can be used to display a view in the workstation UI. If the galaxy
    /// specifies the `framebuffer` feature this framebuffer will be registered as a device.
    ///
    /// When a component is run in that galaxy and also specifies the `framebuffer` feature, the
    /// framebuffer will be served as the view of the component.
    pub framebuffer: Arc<Framebuffer>,

    /// The binder driver registered for this galaxy, indexed by their device type.
    pub binders: RwLock<BTreeMap<DeviceType, Arc<BinderDriver>>>,
}

impl Kernel {
    pub fn new(name: &CStr, features: &[String]) -> Result<Kernel, zx::Status> {
        let unix_address_maker = Box::new(|x: Vec<u8>| -> SocketAddress { SocketAddress::Unix(x) });
        let vsock_address_maker = Box::new(|x: u32| -> SocketAddress { SocketAddress::Vsock(x) });
        let job = fuchsia_runtime::job_default().create_child_job()?;
        job.set_name(name)?;

        Ok(Kernel {
            job,
            starnix_process: fuchsia_runtime::process_self()
                .duplicate(zx::Rights::SAME_RIGHTS)
                .expect("Failed to duplicate process self"),
            pids: RwLock::new(PidTable::new()),
            default_abstract_socket_namespace: AbstractUnixSocketNamespace::new(unix_address_maker),
            default_abstract_vsock_namespace: AbstractVsockSocketNamespace::new(
                vsock_address_maker,
            ),
            cmdline: Vec::new(),
            anon_fs: OnceCell::new(),
            pipe_fs: OnceCell::new(),
            dev_tmp_fs: OnceCell::new(),
            dev_pts_fs: OnceCell::new(),
            proc_fs: OnceCell::new(),
            socket_fs: OnceCell::new(),
            sys_fs: OnceCell::new(),
            selinux_fs: OnceCell::new(),
            device_registry: RwLock::new(DeviceRegistry::new_with_common_devices()),
            features: HashSet::from_iter(features.iter().cloned()),
            framebuffer: Framebuffer::new().expect("Failed to create framebuffer"),
            binders: Default::default(),
        })
    }

    /// Opens a device file (driver) identified by `dev`.
    pub fn open_device(
        &self,
        current_task: &CurrentTask,
        node: &FsNode,
        flags: OpenFlags,
        dev: DeviceType,
        mode: DeviceMode,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let registry = self.device_registry.read();
        registry.open_device(current_task, node, flags, dev, mode)
    }

    pub fn selinux_enabled(&self) -> bool {
        self.features.contains("selinux_enabled")
    }
}
