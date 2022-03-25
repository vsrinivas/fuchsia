// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use parking_lot::{Mutex, RwLock};
use std::collections::{BTreeSet, HashMap};
use std::sync::{Arc, Weak};

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

    // Create ptmx
    fs.root()
        .create_node(b"ptmx", FileMode::IFCHR | FileMode::from_bits(0o666), DeviceType::PTMX)
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
    terminals: RwLock<HashMap<u32, Weak<Terminal>>>,
    pts_ids_set: Mutex<PtsIdsSet>,
}

impl TTYState {
    pub fn new(fs: FileSystemHandle) -> Self {
        Self {
            fs,
            terminals: RwLock::new(HashMap::new()),
            pts_ids_set: Mutex::new(PtsIdsSet::new(DEVPTS_COUNT)),
        }
    }

    pub fn get_next_terminal(self: &Arc<Self>) -> Result<Arc<Terminal>, Errno> {
        let id = self.pts_ids_set.lock().get()?;
        let device_type = get_device_type_for_pts(id);
        let terminal = Arc::new(Terminal::new(self.clone(), id));

        self.fs.root().create_node(
            id.to_string().as_bytes(),
            FileMode::IFCHR | FileMode::from_bits(0o666),
            device_type,
        )?;
        self.terminals.write().insert(id, Arc::downgrade(&terminal));
        Ok(terminal)
    }

    pub fn release_terminal(&self, id: u32) -> Result<(), Errno> {
        let pts_filename = id.to_string();
        match self.fs.root().unlink(pts_filename.as_bytes(), UnlinkKind::NonDirectory) {
            Ok(_) => Ok(()),
            Err(e) => {
                if e.value() == ENOENT.value() {
                    log::warn!("Unable to delete pts id {}.", id);
                    Ok(())
                } else {
                    Err(e)
                }
            }
        }?;

        self.pts_ids_set.lock().release(id);
        self.terminals.write().remove(&id);
        Ok(())
    }
}

struct Terminal {
    state: Arc<TTYState>,
    id: u32,
}

impl Terminal {
    pub fn new(state: Arc<TTYState>, id: u32) -> Self {
        Self { state, id }
    }
}

impl Drop for Terminal {
    fn drop(&mut self) {
        self.state.release_terminal(self.id).unwrap()
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
        let terminal = self.state.get_next_terminal()?;

        Ok(Box::new(DevPtmxFile::new(terminal)))
    }
}

struct DevPtmxFile {
    _terminal: Arc<Terminal>,
}

impl DevPtmxFile {
    pub fn new(terminal: Arc<Terminal>) -> Self {
        Self { _terminal: terminal }
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
        let terminal = self.state.terminals.read().get(&pts_id).ok_or(EIO)?.upgrade().ok_or(EIO)?;
        Ok(Box::new(DevPtsFile::new(terminal)))
    }
}

struct DevPtsFile {}

impl DevPtsFile {
    pub fn new(_terminal: Arc<Terminal>) -> Self {
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

struct PtsIdsSet {
    pts_count: u32,
    next_id: u32,
    reclaimed_ids: BTreeSet<u32>,
}

impl PtsIdsSet {
    pub fn new(pts_count: u32) -> Self {
        Self { pts_count, next_id: 0, reclaimed_ids: BTreeSet::new() }
    }

    pub fn release(&mut self, id: u32) {
        assert!(self.reclaimed_ids.insert(id))
    }

    pub fn get(&mut self) -> Result<u32, Errno> {
        match self.reclaimed_ids.iter().next() {
            Some(e) => {
                let value = e.clone();
                self.reclaimed_ids.remove(&value);
                Ok(value)
            }
            None => {
                if self.next_id < self.pts_count {
                    let id = self.next_id;
                    self.next_id += 1;
                    Ok(id)
                } else {
                    error!(ENOSPC)
                }
            }
        }
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
        let _opened_ptmx = ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        assert!(root.component_lookup(b"0").is_ok());
    }

    #[test]
    fn closing_ptmx_closes_pts() {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        assert!(root.component_lookup(b"0").is_err());
        let ptmx = root.component_lookup(b"ptmx").unwrap();
        ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        assert!(root.component_lookup(b"0").is_err());
    }

    #[test]
    fn pts_are_reused() {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        assert!(root.component_lookup(b"0").is_err());
        let ptmx = root.component_lookup(b"ptmx").unwrap();
        let _opened_ptmx0 = ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        let mut _opened_ptmx1 = ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        let _opened_ptmx2 = ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        assert!(root.component_lookup(b"0").is_ok());
        assert!(root.component_lookup(b"1").is_ok());
        assert!(root.component_lookup(b"2").is_ok());
        std::mem::drop(_opened_ptmx1);
        assert!(root.component_lookup(b"1").is_err());
        _opened_ptmx1 = ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        assert!(root.component_lookup(b"1").is_ok());
    }

    #[test]
    fn opening_inexistant_replica_fails() {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let pts = fs
            .root()
            .create_node(
                b"custom_pts",
                FileMode::IFCHR | FileMode::from_bits(0o666),
                DeviceType::new(DEVPTS_FIRST_MAJOR, 0),
            )
            .unwrap();
        assert!(pts.node.open(&kernel, OpenFlags::RDONLY).is_err());
    }

    #[test]
    fn deleting_pts_nodes_do_not_crash() {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        let ptmx = root.component_lookup(b"ptmx").unwrap();
        let opened_ptmx0 = ptmx.node.open(&kernel, OpenFlags::RDONLY).unwrap();
        assert!(root.component_lookup(b"0").is_ok());
        root.unlink(b"0", UnlinkKind::NonDirectory).unwrap();
        assert!(root.component_lookup(b"0").is_err());
        std::mem::drop(opened_ptmx0);
    }
}
