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
                    tracing::warn!("Unable to delete pts id {}.", id);
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
    locked: RwLock<bool>,
}

impl Terminal {
    pub fn new(state: Arc<TTYState>, id: u32) -> Self {
        Self { state, id, locked: RwLock::new(true) }
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
    terminal: Arc<Terminal>,
}

impl DevPtmxFile {
    pub fn new(terminal: Arc<Terminal>) -> Self {
        Self { terminal }
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
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        match request {
            TIOCGPTN => {
                if user_addr.is_null() {
                    return error!(EINVAL);
                }
                let addr = UserRef::<u32>::new(user_addr);
                let value: u32 = self.terminal.id as u32;
                current_task.mm.write_object(addr, &value)?;
                Ok(SUCCESS)
            }
            TIOCGPTLCK => {
                if user_addr.is_null() {
                    return error!(EINVAL);
                }
                let addr = UserRef::<i32>::new(user_addr);
                let value = if *self.terminal.locked.read() { 1 } else { 0 };
                current_task.mm.write_object(addr, &value)?;
                Ok(SUCCESS)
            }
            TIOCSPTLCK => {
                if user_addr.is_null() {
                    return error!(EINVAL);
                }
                let addr = UserRef::<i32>::new(user_addr);
                let mut value = 0;
                current_task.mm.read_object(addr, &mut value)?;
                *self.terminal.locked.write() = value != 0;
                Ok(SUCCESS)
            }
            _ => {
                tracing::error!("ptmx received unknown ioctl request 0x{:08x}", request);
                error!(EINVAL)
            }
        }
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
        if *terminal.locked.read() {
            return error!(EIO);
        }
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

    fn ioctl<T: zerocopy::AsBytes + zerocopy::FromBytes + Copy>(
        task: &CurrentTask,
        file: &FileHandle,
        command: u32,
        value: &T,
    ) -> Result<T, Errno> {
        let address = map_memory(&task, UserAddress::default(), std::mem::size_of::<T>() as u64);
        let address_ref = UserRef::<T>::new(address);
        task.mm.write_object(address_ref, value)?;
        file.ioctl(&task, command, address)?;
        let mut result = *value;
        task.mm.read_object(address_ref, &mut result)?;
        Ok(result)
    }

    fn open_ptmx_and_unlock(
        kernel: &Kernel,
        task: &CurrentTask,
        fs: &FileSystemHandle,
    ) -> Result<FileHandle, Errno> {
        let ptmx = fs.root().component_lookup(b"ptmx")?;
        let file = FileObject::new_anonymous(
            ptmx.node.open(kernel, OpenFlags::RDONLY)?,
            ptmx.node.clone(),
            OpenFlags::RDWR,
        );

        // Unlock terminal
        ioctl::<i32>(task, &file, TIOCSPTLCK, &0)?;

        Ok(file)
    }

    #[test]
    fn opening_ptmx_creates_pts() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        root.component_lookup(b"0").unwrap_err();
        let _ptmx = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        root.component_lookup(b"0")?;

        Ok(())
    }

    #[test]
    fn closing_ptmx_closes_pts() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        root.component_lookup(b"0").unwrap_err();
        open_ptmx_and_unlock(&kernel, &task, &fs)?;
        root.component_lookup(b"0").unwrap_err();

        Ok(())
    }

    #[test]
    fn pts_are_reused() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();

        let _ptmx0 = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        let mut _ptmx1 = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        let _ptmx2 = open_ptmx_and_unlock(&kernel, &task, &fs)?;

        root.component_lookup(b"0")?;
        root.component_lookup(b"1")?;
        root.component_lookup(b"2")?;

        std::mem::drop(_ptmx1);
        root.component_lookup(b"1").unwrap_err();

        _ptmx1 = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        root.component_lookup(b"1")?;

        Ok(())
    }

    #[test]
    fn opening_inexistant_replica_fails() -> Result<(), anyhow::Error> {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let pts = fs.root().create_node(
            b"custom_pts",
            FileMode::IFCHR | FileMode::from_bits(0o666),
            DeviceType::new(DEVPTS_FIRST_MAJOR, 0),
        )?;
        assert!(pts.node.open(&kernel, OpenFlags::RDONLY).is_err());

        Ok(())
    }

    #[test]
    fn deleting_pts_nodes_do_not_crash() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        let ptmx = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        root.component_lookup(b"0")?;
        root.unlink(b"0", UnlinkKind::NonDirectory)?;
        root.component_lookup(b"0").unwrap_err();

        std::mem::drop(ptmx);

        Ok(())
    }

    #[test]
    fn test_unknown_ioctl() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();

        let ptmx = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        ptmx.ioctl(&task, 42, UserAddress::default()).unwrap_err();

        let pts = root.component_lookup(b"0")?;
        let pts_file = FileObject::new_anonymous(
            pts.node.open(&kernel, OpenFlags::RDONLY)?,
            pts.node.clone(),
            OpenFlags::RDONLY,
        );
        pts_file.ioctl(&task, 42, UserAddress::default()).unwrap_err();

        Ok(())
    }

    #[test]
    fn test_tiocgptn_ioctl() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx0 = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        let ptmx1 = open_ptmx_and_unlock(&kernel, &task, &fs)?;

        let pts0 = ioctl::<u32>(&task, &ptmx0, TIOCGPTN, &0)?;
        assert_eq!(pts0, 0);

        let pts1 = ioctl::<u32>(&task, &ptmx1, TIOCGPTN, &0)?;
        assert_eq!(pts1, 1);

        Ok(())
    }

    #[test]
    fn test_new_terminal_is_locked() -> Result<(), anyhow::Error> {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx = fs.root().component_lookup(b"ptmx")?;
        let _ptmx_file = FileObject::new_anonymous(
            ptmx.node.open(&kernel, OpenFlags::RDONLY)?,
            ptmx.node.clone(),
            OpenFlags::RDWR,
        );

        let pts = fs.root().component_lookup(b"0")?;
        assert_eq!(pts.node.open(&kernel, OpenFlags::RDONLY).map(|_| ()).unwrap_err(), EIO);

        Ok(())
    }

    #[test]
    fn test_lock_ioctls() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx = open_ptmx_and_unlock(&kernel, &task, &fs)?;
        let pts = fs.root().component_lookup(b"0")?;

        // Check that the lock is not set.
        assert_eq!(ioctl::<i32>(&task, &ptmx, TIOCGPTLCK, &0)?, 0);
        // /dev/pts/0 can be opened
        pts.node.open(&kernel, OpenFlags::RDONLY)?;

        // Lock the terminal
        ioctl::<i32>(&task, &ptmx, TIOCSPTLCK, &42)?;
        // Check that the lock is set.
        assert_eq!(ioctl::<i32>(&task, &ptmx, TIOCGPTLCK, &0)?, 1);
        // /dev/pts/0 cannot be opened
        pts.node.open(&kernel, OpenFlags::RDONLY).map(|_| ()).unwrap_err();

        Ok(())
    }
}
