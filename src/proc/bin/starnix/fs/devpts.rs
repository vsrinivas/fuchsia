// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::Arc;

use crate::device::terminal::*;
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
// The device identifier is encoded through the major and minor device identifier of the
// device. Each major identifier can contain 256 pts replicas.
pub const DEVPTS_COUNT: u32 = DEVPTS_MAJOR_COUNT * 256;
// The block size of the node in the devpts file system. Value has been taken from
// https://github.com/google/gvisor/blob/master/test/syscalls/linux/pty.cc
const BLOCK_SIZE: i64 = 1024;

pub fn dev_pts_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.dev_pts_fs.get_or_init(|| init_devpts(kernel))
}

pub fn create_pts_node(fs: &FileSystemHandle, id: u32) -> Result<(), Errno> {
    let device_type = get_device_type_for_pts(id);
    let pts = fs.root().create_node(
        id.to_string().as_bytes(),
        FileMode::IFCHR | FileMode::from_bits(0o666),
        device_type,
    )?;
    pts.node.info_write().blksize = BLOCK_SIZE;
    Ok(())
}

fn init_devpts(kernel: &Kernel) -> FileSystemHandle {
    let fs = TmpFs::new();

    // Create ptmx
    let ptmx = fs
        .root()
        .create_node(b"ptmx", FileMode::IFCHR | FileMode::from_bits(0o666), DeviceType::PTMX)
        .unwrap();
    ptmx.node.info_write().blksize = BLOCK_SIZE;

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
        registry.register_chrdev(DevPtmx::new(fs.clone(), state), DeviceType::PTMX).unwrap();
    }

    fs
}

// Construct the DeviceType associated with the given pts replicas.
fn get_device_type_for_pts(id: u32) -> DeviceType {
    DeviceType::new(DEVPTS_FIRST_MAJOR + id / 256, id % 256)
}

fn unlink_ptr_node_if_exists(fs: &FileSystemHandle, id: u32) -> Result<(), Errno> {
    let pts_filename = id.to_string();
    match fs.root().unlink(pts_filename.as_bytes(), UnlinkKind::NonDirectory) {
        Err(e) if e == ENOENT => Ok(()),
        other => other,
    }
}

struct DevPtmx {
    fs: FileSystemHandle,
    state: Arc<TTYState>,
}

impl DevPtmx {
    pub fn new(fs: FileSystemHandle, state: Arc<TTYState>) -> Self {
        Self { fs, state }
    }
}

impl WithStaticDeviceId for DevPtmx {
    const ID: DeviceType = DeviceType::PTMX;
}

impl DeviceOps for DevPtmx {
    fn open(
        &self,
        _current_task: &CurrentTask,
        _id: DeviceType,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let terminal = self.state.get_next_terminal()?;

        Ok(Box::new(DevPtmxFile::new(self.fs.clone(), terminal)))
    }
}

struct DevPtmxFile {
    fs: FileSystemHandle,
    terminal: Arc<Terminal>,
}

impl DevPtmxFile {
    pub fn new(fs: FileSystemHandle, terminal: Arc<Terminal>) -> Self {
        Self { fs, terminal }
    }
}

impl Drop for DevPtmxFile {
    fn drop(&mut self) {
        unlink_ptr_node_if_exists(&self.fs, self.terminal.id).unwrap();
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
                // Get the therminal id.
                let value: u32 = self.terminal.id as u32;
                current_task.mm.write_object(UserRef::<u32>::new(user_addr), &value)?;
                Ok(SUCCESS)
            }
            TIOCGPTLCK => {
                // Get the lock status.
                let value = if *self.terminal.locked.read() { 1 } else { 0 };
                current_task.mm.write_object(UserRef::<i32>::new(user_addr), &value)?;
                Ok(SUCCESS)
            }
            TIOCSPTLCK => {
                // Lock/Unlock the terminal.
                let mut value = 0;
                current_task.mm.read_object(UserRef::<i32>::new(user_addr), &mut value)?;
                *self.terminal.locked.write() = value != 0;
                Ok(SUCCESS)
            }
            _ => shared_ioctl(&self.terminal, true, _file, current_task, request, user_addr),
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
        current_task: &CurrentTask,
        id: DeviceType,
        _node: &FsNode,
        flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let pts_id = (id.major() - DEVPTS_FIRST_MAJOR) * 256 + id.minor();
        let terminal = self.state.terminals.read().get(&pts_id).ok_or(EIO)?.upgrade().ok_or(EIO)?;
        if *terminal.locked.read() {
            return error!(EIO);
        }
        if !flags.contains(OpenFlags::NOCTTY) {
            // Opening a replica sets the process' controlling TTY when possible. An error indicates it cannot
            // be set, and is ignored silently.
            let _ = current_task.thread_group.set_controlling_terminal(
                current_task,
                &terminal,
                false, /* is_main */
                false, /* steal */
                flags.can_read(),
            );
        }
        Ok(Box::new(DevPtsFile::new(terminal)))
    }
}

struct DevPtsFile {
    terminal: Arc<Terminal>,
}

impl DevPtsFile {
    pub fn new(terminal: Arc<Terminal>) -> Self {
        Self { terminal }
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
        file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        shared_ioctl(&self.terminal, false, file, current_task, request, user_addr)
    }
}

/// The ioctl behaviour common to main and replica terminal file descriptors.
fn shared_ioctl(
    terminal: &Arc<Terminal>,
    is_main: bool,
    file: &FileObject,
    current_task: &CurrentTask,
    request: u32,
    user_addr: UserAddress,
) -> Result<SyscallResult, Errno> {
    match request {
        TIOCSCTTY => {
            // Make the given terminal the controlling terminal of the calling process.
            let mut steal = 0;
            current_task.mm.read_object(UserRef::<i32>::new(user_addr), &mut steal)?;
            current_task.thread_group.set_controlling_terminal(
                current_task,
                terminal,
                is_main,
                steal == 1,
                file.can_read(),
            )?;
            Ok(SUCCESS)
        }
        TIOCGPGRP => {
            // Get the foreground process group.
            let pgid = current_task.thread_group.get_foreground_process_group(terminal, is_main)?;
            current_task.mm.write_object(UserRef::<pid_t>::new(user_addr), &pgid)?;
            Ok(SUCCESS)
        }
        TIOCSPGRP => {
            // Set the foreground process group.
            let mut pgid = 0;
            current_task.mm.read_object(UserRef::<pid_t>::new(user_addr), &mut pgid)?;

            current_task.thread_group.set_foreground_process_group(terminal, is_main, pgid)?;
            Ok(SUCCESS)
        }
        _ => {
            tracing::error!(
                "{} received unknown ioctl request 0x{:08x}",
                if is_main { "ptmx" } else { "pts" },
                request
            );
            error!(EINVAL)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::auth::Credentials;
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

    fn open_file_with_flags(
        task: &CurrentTask,
        fs: &FileSystemHandle,
        name: &FsStr,
        flags: OpenFlags,
    ) -> Result<FileHandle, Errno> {
        let component = fs.root().component_lookup(name)?;
        Ok(FileObject::new_anonymous(
            component.node.open(task, flags)?,
            component.node.clone(),
            flags,
        ))
    }

    fn open_file(
        task: &CurrentTask,
        fs: &FileSystemHandle,
        name: &FsStr,
    ) -> Result<FileHandle, Errno> {
        open_file_with_flags(task, fs, name, OpenFlags::RDWR | OpenFlags::NOCTTY)
    }

    fn open_ptmx_and_unlock(
        task: &CurrentTask,
        fs: &FileSystemHandle,
    ) -> Result<FileHandle, Errno> {
        let file = open_file_with_flags(task, fs, b"ptmx", OpenFlags::RDWR)?;

        // Unlock terminal
        ioctl::<i32>(task, &file, TIOCSPTLCK, &0)?;

        Ok(file)
    }

    #[::fuchsia::test]
    fn opening_ptmx_creates_pts() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        root.component_lookup(b"0").unwrap_err();
        let _ptmx = open_ptmx_and_unlock(&task, &fs)?;
        root.component_lookup(b"0")?;

        Ok(())
    }

    #[::fuchsia::test]
    fn closing_ptmx_closes_pts() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        root.component_lookup(b"0").unwrap_err();
        let ptmx = open_ptmx_and_unlock(&task, &fs)?;
        let _pts = open_file(&task, &fs, b"0")?;
        std::mem::drop(ptmx);
        root.component_lookup(b"0").unwrap_err();

        Ok(())
    }

    #[::fuchsia::test]
    fn pts_are_reused() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();

        let _ptmx0 = open_ptmx_and_unlock(&task, &fs)?;
        let mut _ptmx1 = open_ptmx_and_unlock(&task, &fs)?;
        let _ptmx2 = open_ptmx_and_unlock(&task, &fs)?;

        root.component_lookup(b"0")?;
        root.component_lookup(b"1")?;
        root.component_lookup(b"2")?;

        std::mem::drop(_ptmx1);
        root.component_lookup(b"1").unwrap_err();

        _ptmx1 = open_ptmx_and_unlock(&task, &fs)?;
        root.component_lookup(b"1")?;

        Ok(())
    }

    #[::fuchsia::test]
    fn opening_inexistant_replica_fails() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let pts = fs.root().create_node(
            b"custom_pts",
            FileMode::IFCHR | FileMode::from_bits(0o666),
            DeviceType::new(DEVPTS_FIRST_MAJOR, 0),
        )?;
        assert!(pts.node.open(&task, OpenFlags::RDONLY).is_err());

        Ok(())
    }

    #[::fuchsia::test]
    fn deleting_pts_nodes_do_not_crash() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        let ptmx = open_ptmx_and_unlock(&task, &fs)?;
        root.component_lookup(b"0")?;
        root.unlink(b"0", UnlinkKind::NonDirectory)?;
        root.component_lookup(b"0").unwrap_err();

        std::mem::drop(ptmx);

        Ok(())
    }

    #[::fuchsia::test]
    fn test_unknown_ioctl() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);

        let ptmx = open_ptmx_and_unlock(&task, &fs)?;
        ptmx.ioctl(&task, 42, UserAddress::default()).unwrap_err();

        let pts_file = open_file(&task, &fs, b"0")?;
        pts_file.ioctl(&task, 42, UserAddress::default()).unwrap_err();

        Ok(())
    }

    #[::fuchsia::test]
    fn test_tiocgptn_ioctl() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx0 = open_ptmx_and_unlock(&task, &fs)?;
        let ptmx1 = open_ptmx_and_unlock(&task, &fs)?;

        let pts0 = ioctl::<u32>(&task, &ptmx0, TIOCGPTN, &0)?;
        assert_eq!(pts0, 0);

        let pts1 = ioctl::<u32>(&task, &ptmx1, TIOCGPTN, &0)?;
        assert_eq!(pts1, 1);

        Ok(())
    }

    #[::fuchsia::test]
    fn test_new_terminal_is_locked() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let _ptmx_file = open_file(&task, &fs, b"ptmx")?;

        let pts = fs.root().component_lookup(b"0")?;
        assert_eq!(pts.node.open(&task, OpenFlags::RDONLY).map(|_| ()), Err(EIO));

        Ok(())
    }

    #[::fuchsia::test]
    fn test_lock_ioctls() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx = open_ptmx_and_unlock(&task, &fs)?;
        let pts = fs.root().component_lookup(b"0")?;

        // Check that the lock is not set.
        assert_eq!(ioctl::<i32>(&task, &ptmx, TIOCGPTLCK, &0)?, 0);
        // /dev/pts/0 can be opened
        pts.node.open(&task, OpenFlags::RDONLY)?;

        // Lock the terminal
        ioctl::<i32>(&task, &ptmx, TIOCSPTLCK, &42)?;
        // Check that the lock is set.
        assert_eq!(ioctl::<i32>(&task, &ptmx, TIOCGPTLCK, &0)?, 1);
        // /dev/pts/0 cannot be opened
        pts.node.open(&task, OpenFlags::RDONLY).map(|_| ()).unwrap_err();

        Ok(())
    }

    #[::fuchsia::test]
    fn test_ptmx_blksize() -> Result<(), anyhow::Error> {
        let (kernel, _task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx = fs.root().component_lookup(b"ptmx")?;
        let stat = ptmx.node.stat()?;
        assert_eq!(stat.st_blksize, BLOCK_SIZE);

        Ok(())
    }

    #[::fuchsia::test]
    fn test_attach_terminal_when_open() -> Result<(), anyhow::Error> {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&task, &fs)?;
        // Opening the main terminal should not set the terminal of the session.
        assert!(task
            .thread_group
            .process_group
            .read()
            .session
            .controlling_terminal
            .read()
            .is_none());
        // Opening the terminal should not set the terminal of the session with the NOCTTY flag.
        let _opened_replica2 =
            open_file_with_flags(&task, &fs, b"0", OpenFlags::RDWR | OpenFlags::NOCTTY)?;
        assert!(task
            .thread_group
            .process_group
            .read()
            .session
            .controlling_terminal
            .read()
            .is_none());

        // Opening the replica terminal should set the terminal of the session.
        let _opened_replica2 = open_file_with_flags(&task, &fs, b"0", OpenFlags::RDWR)?;
        assert!(task
            .thread_group
            .process_group
            .read()
            .session
            .controlling_terminal
            .read()
            .is_some());

        Ok(())
    }

    #[::fuchsia::test]
    fn test_attach_terminal() -> Result<(), anyhow::Error> {
        let (kernel, task1) = create_kernel_and_task();
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task2.thread_group.setsid().expect("setsid");

        let fs = dev_pts_fs(&kernel);
        let opened_main = open_ptmx_and_unlock(&task1, &fs)?;
        let opened_replica = open_file(&task2, &fs, b"0")?;

        assert_eq!(ioctl::<i32>(&task1, &opened_main, TIOCGPGRP, &0), Err(ENOTTY));
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCGPGRP, &0), Err(ENOTTY));

        ioctl::<u32>(&task1, &opened_main, TIOCSCTTY, &0).unwrap();
        assert_eq!(
            ioctl::<i32>(&task1, &opened_main, TIOCGPGRP, &0)?,
            task1.thread_group.process_group.read().leader
        );
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCGPGRP, &0), Err(ENOTTY));

        ioctl::<u32>(&task2, &opened_replica, TIOCSCTTY, &0).unwrap();
        assert_eq!(
            ioctl::<i32>(&task2, &opened_replica, TIOCGPGRP, &0)?,
            task2.thread_group.process_group.read().leader
        );

        Ok(())
    }

    #[::fuchsia::test]
    fn test_steal_terminal() -> Result<(), anyhow::Error> {
        let (kernel, task1) = create_kernel_and_task();
        *task1.creds.write() = Credentials::from_passwd("nobody:x:1:1")?;

        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");

        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&task1, &fs)?;
        let wo_opened_replica =
            open_file_with_flags(&task1, &fs, b"0", OpenFlags::WRONLY | OpenFlags::NOCTTY)?;
        assert!(!wo_opened_replica.can_read());

        // FD must be readable for setting the terminal.
        assert_eq!(ioctl::<u32>(&task1, &wo_opened_replica, TIOCSCTTY, &0), Err(EPERM));

        let opened_replica = open_file(&task2, &fs, b"0")?;
        // Task must be session leader for setting the terminal.
        assert_eq!(ioctl::<u32>(&task2, &opened_replica, TIOCSCTTY, &0), Err(EINVAL));

        // Associate terminal to task1.
        ioctl::<u32>(&task1, &opened_replica, TIOCSCTTY, &0).expect("Associate terminal to task1");

        // One cannot associate a terminal to a process that has already one
        assert_eq!(ioctl::<u32>(&task1, &opened_replica, TIOCSCTTY, &0), Err(EINVAL));

        task2.thread_group.setsid().expect("setsid");

        // One cannot associate a terminal that is already associated with another process.
        assert_eq!(ioctl::<u32>(&task2, &opened_replica, TIOCSCTTY, &0), Err(EPERM));

        // One cannot steal a terminal without the CAP_SYS_ADMIN capacility
        assert_eq!(ioctl::<u32>(&task2, &opened_replica, TIOCSCTTY, &1), Err(EPERM));

        // One can steal a terminal with the CAP_SYS_ADMIN capacility
        *task2.creds.write() = Credentials::from_passwd("root:x:0:0")?;
        // But not without specifying that one wants to steal it.
        assert_eq!(ioctl::<u32>(&task2, &opened_replica, TIOCSCTTY, &0), Err(EPERM));
        ioctl::<u32>(&task2, &opened_replica, TIOCSCTTY, &1).expect("Associate terminal to task2");

        assert!(task1
            .thread_group
            .process_group
            .read()
            .session
            .controlling_terminal
            .read()
            .is_none());
        Ok(())
    }

    #[::fuchsia::test]
    fn test_set_foreground_process() -> Result<(), anyhow::Error> {
        let (kernel, init) = create_kernel_and_task();
        let task1 = init
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task1.thread_group.setsid().expect("setsid");
        let task2 = task1
            .clone_task(
                0,
                UserRef::new(UserAddress::default()),
                UserRef::new(UserAddress::default()),
            )
            .expect("clone process");
        task2.thread_group.setpgid(&task2, 0).expect("setpgid");
        let task2_pgid = task2.thread_group.process_group.read().leader;

        assert_ne!(task2_pgid, task1.thread_group.process_group.read().leader);

        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&init, &fs)?;
        let opened_replica = open_file(&task2, &fs, b"0")?;

        // Cannot change the foreground process group if the terminal is not the controlling
        // terminal
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &task2_pgid), Err(ENOTTY));

        // Attach terminal to task1 and task2 session.
        ioctl::<u32>(&task1, &opened_replica, TIOCSCTTY, &0).unwrap();
        // The foreground process group should be the one of task1
        assert_eq!(
            ioctl::<i32>(&task1, &opened_replica, TIOCGPGRP, &0)?,
            task1.thread_group.process_group.read().leader
        );

        // Cannot change the foreground process group to a negative pid.
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &-1), Err(EINVAL));

        // Cannot change the foreground process group to a invalid process group.
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &255), Err(ESRCH));

        // Cannot change the foreground process group to a process group in another session.
        assert_eq!(
            ioctl::<i32>(
                &task2,
                &opened_replica,
                TIOCSPGRP,
                &init.thread_group.process_group.read().leader
            ),
            Err(EPERM)
        );

        // Set the foregound process to task2 process group
        ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &task2_pgid).unwrap();

        // Check that the foreground process has been changed.
        assert_eq!(
            task1
                .thread_group
                .process_group
                .read()
                .session
                .controlling_terminal
                .read()
                .as_ref()
                .unwrap()
                .terminal
                .get_controlling_session(false)
                .as_ref()
                .unwrap()
                .foregound_process_group,
            task2_pgid
        );

        Ok(())
    }
}
