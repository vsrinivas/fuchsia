// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::Mutex;
use std::sync::Arc;

use crate::device::DeviceOps;
use crate::device::WithStaticDeviceId;
use crate::fs::tmpfs::*;
use crate::fs::*;
use crate::syscalls::*;
use crate::task::*;
use crate::types::*;

// See https://www.kernel.org/doc/Documentation/admin-guide/devices.txt
const DEVPTS_FIRST_MAJOR: u32 = 136;
const DEVPTS_MAJOR_COUNT: u32 = 4;
// // The device identifier is encoded through the major and minor device identifier of the
// device. Each major identifier can contain 256 pts replicas.
const DEVPTS_COUNT: u32 = 4 * 256;

// Construct the DeviceType associated with the given pts replicas.
fn get_device_type_for_pts(id: u32) -> DeviceType {
    DeviceType::new(DEVPTS_FIRST_MAJOR + id / 256, id % 256)
}

pub fn dev_pts_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.dev_pts_fs.get_or_init(|| init_devpts(kernel))
}

fn init_devpts(kernel: &Kernel) -> FileSystemHandle {
    let fs = TmpFs::new();
    let root = fs.root();

    // Create ptmx
    root.create_node(b"ptmx", FileMode::IFCHR | FileMode::from_bits(0o666), DeviceType::PTMX)
        .unwrap();

    {
        let state = Arc::new(TTYState::new(fs.clone()));
        let mut registry = kernel.device_registry.write();
        // Register /dev/pts/X device type
        for n in 0..DEVPTS_MAJOR_COUNT {
            registry
                .register_default_chrdev(DevPts::new(state.clone()), DEVPTS_FIRST_MAJOR + n)
                .unwrap();
        }
        // Register ptmx device type
        registry.register_chrdev(DevPtmx::new(state), DeviceType::PTMX).unwrap();
    }

    fs
}

struct TTYState {
    fs: FileSystemHandle,
    next_id: Mutex<u32>,
}

impl TTYState {
    pub fn new(fs: FileSystemHandle) -> Self {
        Self { fs, next_id: Mutex::new(0) }
    }

    pub fn get_next_id(&self) -> u32 {
        let mut next_id = self.next_id.lock();
        let id = *next_id;
        *next_id = (*next_id + 1) % DEVPTS_COUNT;
        id
    }
}

struct DevPtmx {
    state: Arc<TTYState>,
}

impl DevPtmx {
    pub fn new(state: Arc<TTYState>) -> Self {
        Self { state }
    }
}

impl WithStaticDeviceId for DevPtmx {
    const ID: DeviceType = DeviceType::PTMX;
}

impl DeviceOps for DevPtmx {
    fn open(
        &self,
        _id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let id = self.state.get_next_id();
        let device_type = get_device_type_for_pts(id);
        self.state
            .fs
            .root()
            .create_node(
                id.to_string().as_bytes(),
                FileMode::IFCHR | FileMode::from_bits(0o666),
                device_type,
            )
            .unwrap();

        Ok(Box::new(DevPtmxFile::new(self.state.clone(), id)))
    }
}

struct DevPtmxFile {}

impl DevPtmxFile {
    pub fn new(_state: Arc<TTYState>, _id: u32) -> Self {
        Self {}
    }
}

impl FileOps for DevPtmxFile {
    fileops_impl_nonseekable!();

    fn close(&self, _file: &FileObject) {}

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _waiter: &Arc<Waiter>,
        _events: FdEvents,
        _handler: EventHandler,
    ) -> WaitKey {
        WaitKey::empty()
    }

    fn cancel_wait(
        &self,
        _current_task: &CurrentTask,
        _waiter: &Arc<Waiter>,
        _key: WaitKey,
    ) -> bool {
        false
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        FdEvents::empty()
    }

    fn fcntl(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _cmd: u32,
        _arg: u64,
    ) -> Result<SyscallResult, Errno> {
        error!(EOPNOTSUPP)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _request: u32,
        _user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        error!(EOPNOTSUPP)
    }
}

struct DevPts {
    state: Arc<TTYState>,
}

impl DevPts {
    pub fn new(state: Arc<TTYState>) -> Self {
        Self { state }
    }
}

impl DeviceOps for DevPts {
    fn open(
        &self,
        id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let pts_id = (id.major() - DEVPTS_FIRST_MAJOR) * 256 + id.minor();
        Ok(Box::new(DevPtsFile::new(self.state.clone(), pts_id)))
    }
}

struct DevPtsFile {}

impl DevPtsFile {
    pub fn new(_state: Arc<TTYState>, _id: u32) -> Self {
        Self {}
    }
}

impl FileOps for DevPtsFile {
    fileops_impl_nonseekable!();

    fn close(&self, _file: &FileObject) {}

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EOPNOTSUPP)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _waiter: &Arc<Waiter>,
        _events: FdEvents,
        _handler: EventHandler,
    ) -> WaitKey {
        WaitKey::empty()
    }

    fn cancel_wait(
        &self,
        _current_task: &CurrentTask,
        _waiter: &Arc<Waiter>,
        _key: WaitKey,
    ) -> bool {
        false
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        FdEvents::empty()
    }

    fn fcntl(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _cmd: u32,
        _arg: u64,
    ) -> Result<SyscallResult, Errno> {
        error!(EOPNOTSUPP)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _request: u32,
        _user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        error!(EOPNOTSUPP)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;

    #[test]
    fn opening_ptmx_creates_pts() {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        assert!(root.component_lookup(b"0").is_err());
        let ptmx = root.component_lookup(b"ptmx").unwrap();
        ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        assert!(root.component_lookup(b"0").is_ok());
    }
}
