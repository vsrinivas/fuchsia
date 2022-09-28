// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::{Arc, Weak};

use crate::auth::FsCred;
use crate::device::terminal::*;
use crate::device::DeviceOps;
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

// The inode of the different node in the devpts filesystem.
const ROOT_INODE: ino_t = 1;
const PTMX_INODE: ino_t = 2;
const FIRST_PTS_INODE: ino_t = 3;

pub fn dev_pts_fs(kernel: &Kernel) -> &FileSystemHandle {
    kernel.dev_pts_fs.get_or_init(|| init_devpts(kernel))
}

fn init_devpts(kernel: &Kernel) -> FileSystemHandle {
    let state = Arc::new(TTYState::new());
    let device = DevPtsDevice::new(state.clone());
    {
        let mut registry = kernel.device_registry.write();
        // Register /dev/pts/X device type
        for n in 0..DEVPTS_MAJOR_COUNT {
            registry
                .register_chrdev_major(device.clone(), DEVPTS_FIRST_MAJOR + n)
                .expect("Registering pts device");
        }
        // Register tty/ptmx device major
        registry.register_chrdev_major(device, TTY_ALT_MAJOR).unwrap();
    }
    let fs = FileSystem::new(kernel, DevPtsFs);
    fs.set_root(DevPtsRootDir { state });
    assert!(fs.root().node.inode_num == ROOT_INODE);
    fs
}

struct DevPtsFs;
impl FileSystemOps for DevPtsFs {
    fn statfs(&self, _fs: &FileSystem) -> Result<statfs, Errno> {
        Ok(statfs::default(DEVPTS_SUPER_MAGIC))
    }

    fn generate_node_ids(&self) -> bool {
        true
    }
}

// Construct the DeviceType associated with the given pts replicas.
fn get_device_type_for_pts(id: u32) -> DeviceType {
    DeviceType::new(DEVPTS_FIRST_MAJOR + id / 256, id % 256)
}

struct DevPtsRootDir {
    state: Arc<TTYState>,
}

impl FsNodeOps for DevPtsRootDir {
    fs_node_impl_dir_readonly!();

    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        let mut result = vec![];
        result.push(VecDirectoryEntry {
            entry_type: DirectoryEntryType::CHR,
            name: b"ptmx".to_vec(),
            inode: Some(PTMX_INODE),
        });
        for (id, terminal) in self.state.terminals.read().iter() {
            if let Some(terminal) = terminal.upgrade() {
                if !terminal.read().is_main_closed() {
                    result.push(VecDirectoryEntry {
                        entry_type: DirectoryEntryType::CHR,
                        name: format!("{}", id).as_bytes().to_vec(),
                        inode: Some((*id as ino_t) + FIRST_PTS_INODE),
                    });
                }
            }
        }
        Ok(VecDirectory::new_file(result))
    }

    fn lookup(
        &self,
        node: &FsNode,
        _current_task: &CurrentTask,
        name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        let name = std::str::from_utf8(name).map_err(|_| errno!(ENOENT))?;
        if name == "ptmx" {
            let node = node.fs().create_node_with_id(
                Box::new(SpecialNode),
                PTMX_INODE,
                mode!(IFCHR, 0o666),
                FsCred::root(),
            );
            {
                let mut info = node.info_write();
                info.rdev = DeviceType::PTMX;
                info.blksize = BLOCK_SIZE;
            }
            return Ok(node);
        }
        if let Ok(id) = name.parse::<u32>() {
            let terminal = self.state.terminals.read().get(&id).and_then(Weak::upgrade);
            if let Some(terminal) = terminal {
                if !terminal.read().is_main_closed() {
                    let node = node.fs().create_node_with_id(
                        Box::new(SpecialNode),
                        (id as ino_t) + FIRST_PTS_INODE,
                        mode!(IFCHR, 0o620),
                        terminal.fscred.clone(),
                    );
                    {
                        let mut info = node.info_write();
                        info.rdev = get_device_type_for_pts(id);
                        info.blksize = BLOCK_SIZE;
                        // TODO(qsr): set gid to the tty group
                        info.gid = 0;
                    }
                    return Ok(node);
                }
            }
        }
        error!(ENOENT)
    }
}

struct DevPtsDevice {
    state: Arc<TTYState>,
}

impl DevPtsDevice {
    pub fn new(state: Arc<TTYState>) -> Arc<Self> {
        Arc::new(Self { state })
    }
}

impl DeviceOps for Arc<DevPtsDevice> {
    fn open(
        &self,
        current_task: &CurrentTask,
        id: DeviceType,
        _node: &FsNode,
        flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        match id {
            // /dev/ptmx
            DeviceType::PTMX => {
                let terminal = self.state.get_next_terminal(current_task)?;

                Ok(Box::new(DevPtmxFile::new(terminal)))
            }
            // /dev/tty
            DeviceType::TTY => {
                let controlling_terminal = current_task
                    .thread_group
                    .read()
                    .process_group
                    .session
                    .read()
                    .controlling_terminal
                    .clone();
                if let Some(controlling_terminal) = controlling_terminal {
                    if controlling_terminal.is_main {
                        Ok(Box::new(DevPtmxFile::new(controlling_terminal.terminal)))
                    } else {
                        Ok(Box::new(DevPtsFile::new(controlling_terminal.terminal)))
                    }
                } else {
                    error!(ENXIO)
                }
            }
            _ if id.major() < DEVPTS_FIRST_MAJOR
                || id.major() >= DEVPTS_FIRST_MAJOR + DEVPTS_MAJOR_COUNT =>
            {
                error!(ENODEV)
            }
            // /dev/pts/??
            _ => {
                let pts_id = (id.major() - DEVPTS_FIRST_MAJOR) * 256 + id.minor();
                let terminal = self
                    .state
                    .terminals
                    .read()
                    .get(&pts_id)
                    .and_then(Weak::upgrade)
                    .ok_or_else(|| errno!(EIO))?;
                if terminal.read().locked {
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
    }
}

struct DevPtmxFile {
    terminal: Arc<Terminal>,
}

impl DevPtmxFile {
    pub fn new(terminal: Arc<Terminal>) -> Self {
        terminal.main_open();
        Self { terminal }
    }
}

impl FileOps for DevPtmxFile {
    fileops_impl_nonseekable!();

    fn close(&self, file: &FileObject) {
        self.terminal.main_close();
        file.fs.root().remove_child(format!("{}", self.terminal.id).as_bytes());
    }

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        self.terminal.main_read(current_task, data)
    }

    fn write(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        self.terminal.main_write(current_task, data)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        self.terminal.main_wait_async(waiter, events, handler, options)
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, _waiter: &Waiter, key: WaitKey) {
        self.terminal.main_cancel_wait(key);
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        self.terminal.main_query_events()
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
                let value = if self.terminal.read().locked { 1 } else { 0 };
                current_task.mm.write_object(UserRef::<i32>::new(user_addr), &value)?;
                Ok(SUCCESS)
            }
            TIOCSPTLCK => {
                // Lock/Unlock the terminal.
                let value = current_task.mm.read_object(UserRef::<i32>::new(user_addr))?;
                self.terminal.write().locked = value != 0;
                Ok(SUCCESS)
            }
            _ => shared_ioctl(&self.terminal, true, _file, current_task, request, user_addr),
        }
    }
}

struct DevPtsFile {
    terminal: Arc<Terminal>,
}

impl DevPtsFile {
    pub fn new(terminal: Arc<Terminal>) -> Self {
        terminal.replica_open();
        Self { terminal }
    }
}

impl FileOps for DevPtsFile {
    fileops_impl_nonseekable!();

    fn close(&self, _file: &FileObject) {
        self.terminal.replica_close();
    }

    fn read(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        self.terminal.replica_read(current_task, data)
    }

    fn write(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        self.terminal.replica_write(current_task, data)
    }

    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        self.terminal.replica_wait_async(waiter, events, handler, options)
    }

    fn cancel_wait(&self, _current_task: &CurrentTask, _waiter: &Waiter, key: WaitKey) {
        self.terminal.replica_cancel_wait(key);
    }

    fn query_events(&self, _current_task: &CurrentTask) -> FdEvents {
        self.terminal.replica_query_events()
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
        FIONREAD => {
            // Get the main terminal available bytes for reading.
            let value = terminal.read().get_available_read_size(is_main) as u32;
            current_task.mm.write_object(UserRef::<u32>::new(user_addr), &value)?;
            Ok(SUCCESS)
        }
        TIOCSCTTY => {
            // Make the given terminal the controlling terminal of the calling process.
            let steal = !user_addr.is_null();
            current_task.thread_group.set_controlling_terminal(
                current_task,
                terminal,
                is_main,
                steal,
                file.can_read(),
            )?;
            Ok(SUCCESS)
        }
        TIOCNOTTY => {
            // Release the controlling terminal.
            current_task.thread_group.release_controlling_terminal(
                current_task,
                terminal,
                is_main,
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
            let pgid = current_task.mm.read_object(UserRef::<pid_t>::new(user_addr))?;
            current_task.thread_group.set_foreground_process_group(
                current_task,
                terminal,
                is_main,
                pgid,
            )?;
            Ok(SUCCESS)
        }
        TIOCGWINSZ => {
            // Get the window size
            current_task.mm.write_object(
                UserRef::<uapi::winsize>::new(user_addr),
                &terminal.read().window_size,
            )?;
            Ok(SUCCESS)
        }
        TIOCSWINSZ => {
            // Set the window size
            terminal.write().window_size =
                current_task.mm.read_object(UserRef::<uapi::winsize>::new(user_addr))?;

            // Send a SIGWINCH signal to the foreground process group.
            let foreground_process_group = terminal
                .read()
                .get_controlling_session(is_main)
                .as_ref()
                .and_then(|cs| cs.foregound_process_group.upgrade());
            if let Some(process_group) = foreground_process_group {
                process_group.send_signals(&[SIGWINCH]);
            }
            Ok(SUCCESS)
        }
        TCGETS => {
            // N.B. TCGETS on the main terminal actually returns the configuration of the replica
            // end.
            current_task.mm.write_object(
                UserRef::<uapi::termios>::new(user_addr),
                terminal.read().termios(),
            )?;
            Ok(SUCCESS)
        }
        TCSETS => {
            // N.B. TCSETS on the main terminal actually affects the configuration of the replica
            // end.
            let termios = current_task.mm.read_object(UserRef::<uapi::termios>::new(user_addr))?;
            terminal.set_termios(termios);
            Ok(SUCCESS)
        }
        TCSETSF => {
            // This should drain the output queue and discard the pending input first.
            let termios = current_task.mm.read_object(UserRef::<uapi::termios>::new(user_addr))?;
            terminal.set_termios(termios);
            Ok(SUCCESS)
        }
        TCSETSW => {
            // TODO(qsr): This should drain the output queue first.
            let termios = current_task.mm.read_object(UserRef::<uapi::termios>::new(user_addr))?;
            terminal.set_termios(termios);
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
    use crate::auth::{Credentials, FsCred};
    use crate::fs::tmpfs::TmpFs;
    use crate::testing::*;

    fn ioctl<T: zerocopy::AsBytes + zerocopy::FromBytes + Copy>(
        task: &CurrentTask,
        file: &FileHandle,
        command: u32,
        value: &T,
    ) -> Result<T, Errno> {
        let address = map_memory(task, UserAddress::default(), std::mem::size_of::<T>() as u64);
        let address_ref = UserRef::<T>::new(address);
        task.mm.write_object(address_ref, value)?;
        file.ioctl(task, command, address)?;
        task.mm.read_object(address_ref)
    }

    fn set_controlling_terminal(
        task: &CurrentTask,
        file: &FileHandle,
        steal: bool,
    ) -> Result<SyscallResult, Errno> {
        file.ioctl(task, TIOCSCTTY, UserAddress::from(if steal { 1 } else { 0 }))
    }

    fn open_file_with_flags(
        task: &CurrentTask,
        fs: &FileSystemHandle,
        name: &FsStr,
        flags: OpenFlags,
    ) -> Result<FileHandle, Errno> {
        let component = fs.root().component_lookup(task, name)?;
        Ok(FileObject::new(
            component.node.open(task, flags, true)?,
            NamespaceNode::new_anonymous(component.clone()),
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
    fn opening_ptmx_creates_pts() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        root.component_lookup(&task, b"0").unwrap_err();
        let _ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        root.component_lookup(&task, b"0").expect("pty");
    }

    #[::fuchsia::test]
    fn closing_ptmx_closes_pts() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();
        root.component_lookup(&task, b"0").unwrap_err();
        let ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let _pts = open_file(&task, fs, b"0").expect("open file");
        std::mem::drop(ptmx);
        root.component_lookup(&task, b"0").unwrap_err();
    }

    #[::fuchsia::test]
    fn pts_are_reused() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let root = fs.root();

        let _ptmx0 = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let mut _ptmx1 = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let _ptmx2 = open_ptmx_and_unlock(&task, fs).expect("ptmx");

        root.component_lookup(&task, b"0").expect("component_lookup");
        root.component_lookup(&task, b"1").expect("component_lookup");
        root.component_lookup(&task, b"2").expect("component_lookup");

        std::mem::drop(_ptmx1);
        root.component_lookup(&task, b"1").unwrap_err();

        _ptmx1 = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        root.component_lookup(&task, b"1").expect("component_lookup");
    }

    #[::fuchsia::test]
    fn opening_inexistant_replica_fails() {
        let (kernel, task) = create_kernel_and_task();
        // Initialize pts devices
        dev_pts_fs(&kernel);
        let fs = TmpFs::new_fs(&kernel);
        let pts = fs
            .root()
            .create_node(
                &task,
                b"custom_pts",
                mode!(IFCHR, 0o666),
                DeviceType::new(DEVPTS_FIRST_MAJOR, 0),
                FsCred::root(),
            )
            .expect("custom_pts");
        assert!(pts.node.open(&task, OpenFlags::RDONLY, true).is_err());
    }

    #[::fuchsia::test]
    fn test_open_tty() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let devfs = crate::fs::devtmpfs::dev_tmp_fs(&task);

        let ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        set_controlling_terminal(&task, &ptmx, false).expect("set_controlling_terminal");
        let tty = open_file_with_flags(&task, devfs, b"tty", OpenFlags::RDWR).expect("tty");
        // Check that tty is the main terminal by calling the ioctl TIOCGPTN and checking it is
        // has the same result has on ptmx.
        assert_eq!(
            ioctl::<i32>(&task, &tty, TIOCGPTN, &0),
            ioctl::<i32>(&task, &ptmx, TIOCGPTN, &0)
        );

        // Detach the controlling terminal.
        ioctl::<i32>(&task, &ptmx, TIOCNOTTY, &0).expect("detach terminal");
        let pts = open_file(&task, fs, b"0").expect("open file");
        set_controlling_terminal(&task, &pts, false).expect("set_controlling_terminal");
        let tty = open_file_with_flags(&task, devfs, b"tty", OpenFlags::RDWR).expect("tty");
        // TIOCGPTN is not implemented on replica terminals
        assert!(ioctl::<i32>(&task, &tty, TIOCGPTN, &0).is_err());
    }

    #[::fuchsia::test]
    fn test_unknown_ioctl() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);

        let ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        assert_eq!(ptmx.ioctl(&task, 42, UserAddress::default()), error!(EINVAL));

        let pts_file = open_file(&task, fs, b"0").expect("open file");
        assert_eq!(pts_file.ioctl(&task, 42, UserAddress::default()), error!(EINVAL));
    }

    #[::fuchsia::test]
    fn test_tiocgptn_ioctl() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx0 = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let ptmx1 = open_ptmx_and_unlock(&task, fs).expect("ptmx");

        let pts0 = ioctl::<u32>(&task, &ptmx0, TIOCGPTN, &0).expect("ioctl");
        assert_eq!(pts0, 0);

        let pts1 = ioctl::<u32>(&task, &ptmx1, TIOCGPTN, &0).expect("ioctl");
        assert_eq!(pts1, 1);
    }

    #[::fuchsia::test]
    fn test_new_terminal_is_locked() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let _ptmx_file = open_file(&task, fs, b"ptmx").expect("open file");

        let pts = fs.root().component_lookup(&task, b"0").expect("component_lookup");
        assert_eq!(pts.node.open(&task, OpenFlags::RDONLY, true).map(|_| ()), error!(EIO));
    }

    #[::fuchsia::test]
    fn test_lock_ioctls() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let pts = fs.root().component_lookup(&task, b"0").expect("component_lookup");

        // Check that the lock is not set.
        assert_eq!(ioctl::<i32>(&task, &ptmx, TIOCGPTLCK, &0), Ok(0));
        // /dev/pts/0 can be opened
        pts.node.open(&task, OpenFlags::RDONLY, true).expect("open");

        // Lock the terminal
        ioctl::<i32>(&task, &ptmx, TIOCSPTLCK, &42).expect("ioctl");
        // Check that the lock is set.
        assert_eq!(ioctl::<i32>(&task, &ptmx, TIOCGPTLCK, &0), Ok(1));
        // /dev/pts/0 cannot be opened
        assert_eq!(pts.node.open(&task, OpenFlags::RDONLY, true).map(|_| ()), error!(EIO));
    }

    #[::fuchsia::test]
    fn test_ptmx_stats() {
        let (kernel, task) = create_kernel_and_task();
        task.set_creds(Credentials::from_passwd("nobody:x:22:22").expect("credentials"));
        let fs = dev_pts_fs(&kernel);
        let ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let ptmx_stat = ptmx.node().stat().expect("stat");
        assert_eq!(ptmx_stat.st_blksize, BLOCK_SIZE);
        let pts = open_file(&task, fs, b"0").expect("open file");
        let pts_stats = pts.node().stat().expect("stat");
        assert_eq!(pts_stats.st_mode & FileMode::PERMISSIONS.bits(), 0o620);
        assert_eq!(pts_stats.st_uid, 22);
        // TODO(qsr): Check that gid is tty.
    }

    #[::fuchsia::test]
    fn test_attach_terminal_when_open() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        // Opening the main terminal should not set the terminal of the session.
        assert!(task
            .thread_group
            .read()
            .process_group
            .session
            .read()
            .controlling_terminal
            .is_none());
        // Opening the terminal should not set the terminal of the session with the NOCTTY flag.
        let _opened_replica2 =
            open_file_with_flags(&task, fs, b"0", OpenFlags::RDWR | OpenFlags::NOCTTY)
                .expect("open file");
        assert!(task
            .thread_group
            .read()
            .process_group
            .session
            .read()
            .controlling_terminal
            .is_none());

        // Opening the replica terminal should set the terminal of the session.
        let _opened_replica2 =
            open_file_with_flags(&task, fs, b"0", OpenFlags::RDWR).expect("open file");
        assert!(task
            .thread_group
            .read()
            .process_group
            .session
            .read()
            .controlling_terminal
            .is_some());
    }

    #[::fuchsia::test]
    fn test_attach_terminal() {
        let (kernel, task1) = create_kernel_and_task();
        let task2 = task1.clone_task_for_test(0);
        task2.thread_group.setsid().expect("setsid");

        let fs = dev_pts_fs(&kernel);
        let opened_main = open_ptmx_and_unlock(&task1, fs).expect("ptmx");
        let opened_replica = open_file(&task2, fs, b"0").expect("open file");

        assert_eq!(ioctl::<i32>(&task1, &opened_main, TIOCGPGRP, &0), error!(ENOTTY));
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCGPGRP, &0), error!(ENOTTY));

        set_controlling_terminal(&task1, &opened_main, false).unwrap();
        assert_eq!(
            ioctl::<i32>(&task1, &opened_main, TIOCGPGRP, &0),
            Ok(task1.thread_group.read().process_group.leader)
        );
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCGPGRP, &0), error!(ENOTTY));

        set_controlling_terminal(&task2, &opened_replica, false).unwrap();
        assert_eq!(
            ioctl::<i32>(&task2, &opened_replica, TIOCGPGRP, &0),
            Ok(task2.thread_group.read().process_group.leader)
        );
    }

    #[::fuchsia::test]
    fn test_steal_terminal() {
        let (kernel, task1) = create_kernel_and_task();
        task1.set_creds(Credentials::from_passwd("nobody:x:1:1").expect("credentials"));

        let task2 = task1.clone_task_for_test(0);

        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&task1, fs).expect("ptmx");
        let wo_opened_replica =
            open_file_with_flags(&task1, fs, b"0", OpenFlags::WRONLY | OpenFlags::NOCTTY)
                .expect("open file");
        assert!(!wo_opened_replica.can_read());

        // FD must be readable for setting the terminal.
        assert_eq!(set_controlling_terminal(&task1, &wo_opened_replica, false), error!(EPERM));

        let opened_replica = open_file(&task2, fs, b"0").expect("open file");
        // Task must be session leader for setting the terminal.
        assert_eq!(set_controlling_terminal(&task2, &opened_replica, false), error!(EINVAL));

        // Associate terminal to task1.
        set_controlling_terminal(&task1, &opened_replica, false)
            .expect("Associate terminal to task1");

        // One cannot associate a terminal to a process that has already one
        assert_eq!(set_controlling_terminal(&task1, &opened_replica, false), error!(EINVAL));

        task2.thread_group.setsid().expect("setsid");

        // One cannot associate a terminal that is already associated with another process.
        assert_eq!(set_controlling_terminal(&task2, &opened_replica, false), error!(EPERM));

        // One cannot steal a terminal without the CAP_SYS_ADMIN capacility
        assert_eq!(set_controlling_terminal(&task2, &opened_replica, true), error!(EPERM));

        // One can steal a terminal with the CAP_SYS_ADMIN capacility
        task2.set_creds(Credentials::from_passwd("root:x:0:0").expect("credentials"));
        // But not without specifying that one wants to steal it.
        assert_eq!(set_controlling_terminal(&task2, &opened_replica, false), error!(EPERM));
        set_controlling_terminal(&task2, &opened_replica, true)
            .expect("Associate terminal to task2");

        assert!(task1
            .thread_group
            .read()
            .process_group
            .session
            .read()
            .controlling_terminal
            .is_none());
    }

    #[::fuchsia::test]
    fn test_set_foreground_process() {
        let (kernel, init) = create_kernel_and_task();
        let task1 = init.clone_task_for_test(0);
        task1.thread_group.setsid().expect("setsid");
        let task2 = task1.clone_task_for_test(0);
        task2.thread_group.setpgid(&task2, 0).expect("setpgid");
        let task2_pgid = task2.thread_group.read().process_group.leader;

        assert_ne!(task2_pgid, task1.thread_group.read().process_group.leader);

        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&init, fs).expect("ptmx");
        let opened_replica = open_file(&task2, fs, b"0").expect("open file");

        // Cannot change the foreground process group if the terminal is not the controlling
        // terminal
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &task2_pgid), error!(ENOTTY));

        // Attach terminal to task1 and task2 session.
        set_controlling_terminal(&task1, &opened_replica, false).unwrap();
        // The foreground process group should be the one of task1
        assert_eq!(
            ioctl::<i32>(&task1, &opened_replica, TIOCGPGRP, &0),
            Ok(task1.thread_group.read().process_group.leader)
        );

        // Cannot change the foreground process group to a negative pid.
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &-1), error!(EINVAL));

        // Cannot change the foreground process group to a invalid process group.
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &255), error!(ESRCH));

        // Cannot change the foreground process group to a process group in another session.
        let init_pgid = init.thread_group.read().process_group.leader;
        assert_eq!(ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &init_pgid,), error!(EPERM));

        // Set the foregound process to task2 process group
        ioctl::<i32>(&task2, &opened_replica, TIOCSPGRP, &task2_pgid).unwrap();

        // Check that the foreground process has been changed.
        let terminal = Arc::clone(
            &task1
                .thread_group
                .read()
                .process_group
                .session
                .read()
                .controlling_terminal
                .as_ref()
                .unwrap()
                .terminal,
        );
        assert_eq!(
            terminal
                .read()
                .get_controlling_session(false)
                .as_ref()
                .unwrap()
                .foregound_process_group_leader,
            task2_pgid
        );
    }

    #[::fuchsia::test]
    fn test_detach_session() {
        let (kernel, task1) = create_kernel_and_task();
        let task2 = task1.clone_task_for_test(0);
        task2.thread_group.setsid().expect("setsid");

        let fs = dev_pts_fs(&kernel);
        let _opened_main = open_ptmx_and_unlock(&task1, fs).expect("ptmx");
        let opened_replica = open_file(&task1, fs, b"0").expect("open file");

        // Cannot detach the controlling terminal when none is attached terminal
        assert_eq!(ioctl::<i32>(&task1, &opened_replica, TIOCNOTTY, &0), error!(ENOTTY));

        set_controlling_terminal(&task2, &opened_replica, false).expect("set controlling terminal");

        // Cannot detach the controlling terminal when not the session leader.
        assert_eq!(ioctl::<i32>(&task1, &opened_replica, TIOCNOTTY, &0), error!(ENOTTY));

        // Detach the terminal
        ioctl::<i32>(&task2, &opened_replica, TIOCNOTTY, &0).expect("detach terminal");
        assert!(task2
            .thread_group
            .read()
            .process_group
            .session
            .read()
            .controlling_terminal
            .is_none());
    }

    #[::fuchsia::test]
    fn test_send_data_back_and_forth() {
        let (kernel, task) = create_kernel_and_task();
        let fs = dev_pts_fs(&kernel);
        let ptmx = open_ptmx_and_unlock(&task, fs).expect("ptmx");
        let pts = open_file(&task, fs, b"0").expect("open file");

        let mm = &task.mm;
        let addr = map_memory(&task, UserAddress::default(), 4096);
        let mut buffer = vec![0; 4096];
        let get_buffer = |size: usize| [UserBuffer { address: addr, length: size }];

        let has_data_ready_to_read = |fd: &FileHandle| fd.query_events(&task) & FdEvents::POLLIN;

        let write_and_assert = |fd: &FileHandle, data: &[u8]| {
            mm.write_all(&get_buffer(data.len()), data).expect("write_all");
            assert_eq!(fd.write(&task, &get_buffer(data.len())).expect("write"), data.len());
        };

        let mut read_and_check = |fd: &FileHandle, data: &[u8]| {
            assert!(has_data_ready_to_read(fd));
            assert_eq!(fd.read(&task, &get_buffer(data.len() + 1)).expect("read"), data.len());
            mm.read_all(&get_buffer(data.len()), &mut buffer).expect("read_all");

            assert_eq!(data, buffer[..data.len()].to_vec());
        };

        let hello_buffer = b"hello\n";
        let hello_transformed_buffer = b"hello\r\n";

        // Main to replica
        write_and_assert(&ptmx, hello_buffer);
        read_and_check(&pts, hello_buffer);

        // Data has been echoed
        read_and_check(&ptmx, hello_transformed_buffer);

        // Replica to main
        write_and_assert(&pts, hello_buffer);
        read_and_check(&ptmx, hello_transformed_buffer);

        // Data has not been echoed
        assert!(!has_data_ready_to_read(&pts));
    }
}
