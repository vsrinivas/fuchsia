// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use once_cell::sync::OnceCell;
use std::sync::{Arc, Weak};

use crate::auth::FsCred;
use crate::device::DeviceMode;
use crate::fs::pipe::Pipe;
use crate::fs::socket::*;
use crate::fs::*;
use crate::lock::{Mutex, RwLock, RwLockReadGuard, RwLockWriteGuard};
use crate::task::*;
use crate::types::as_any::AsAny;
use crate::types::*;

pub struct FsNode {
    /// The FsNodeOps for this FsNode.
    ///
    /// The FsNodeOps are implemented by the individual file systems to provide
    /// specific behaviors for this FsNode.
    ops: Box<dyn FsNodeOps>,

    /// The FileSystem that owns this FsNode's tree.
    fs: Weak<FileSystem>,

    /// The inode number for this FsNode.
    pub inode_num: ino_t,

    /// The pipe located at this node, if any.
    ///
    /// Used if, and only if, the node has a mode of FileMode::IFIFO.
    fifo: Option<Arc<Mutex<Pipe>>>,

    /// The socket located at this node, if any.
    ///
    /// Used if, and only if, the node has a mode of FileMode::IFSOCK.
    ///
    /// The `OnceCell` is initialized when a new socket node is created:
    ///   - in `Socket::new` (e.g., from `sys_socket`)
    ///   - in `sys_bind`, before the node is given a name (i.e., before it could be accessed by
    ///     others)
    socket: OnceCell<SocketHandle>,

    /// A RwLock to synchronize append operations for this node.
    ///
    /// FileObjects writing with O_APPEND should grab a write() lock on this
    /// field to ensure they operate sequentially. FileObjects writing without
    /// O_APPEND should grab read() lock so that they can operate in parallel.
    pub append_lock: RwLock<()>,

    /// Mutable information about this node.
    ///
    /// This data is used to populate the stat_t structure.
    info: RwLock<FsNodeInfo>,

    /// Information about the locking information on this node.
    ///
    /// No other lock on this object may be taken while this lock is held.
    flock_info: Mutex<FlockInfo>,

    /// Records locks associated with this node.
    record_locks: RecordLocks,
}

pub type FsNodeHandle = Arc<FsNode>;

#[derive(Default)]
pub struct FsNodeInfo {
    pub mode: FileMode,
    pub size: usize,
    pub storage_size: usize,
    pub blksize: i64,
    pub uid: uid_t,
    pub gid: gid_t,
    pub link_count: u64,
    pub time_create: zx::Time,
    pub time_access: zx::Time,
    pub time_modify: zx::Time,
    pub rdev: DeviceType,
}

#[derive(Default)]
struct FlockInfo {
    /// Whether the node is currently locked. The meaning of the different values are:
    /// - `None`: The node is not locked.
    /// - `Some(false)`: The node is locked non exclusively.
    /// - `Some(true)`: The node is locked exclusively.
    locked_exclusive: Option<bool>,
    /// The FileObject that hold the lock.
    locking_handles: Vec<Weak<FileObject>>,
    /// The queue to notify process waiting on the lock.
    wait_queue: WaitQueue,
}

impl FlockInfo {
    /// Removes all file handle not holding `predicate` from the list of object holding the lock. If
    /// this empties the list, unlocks the node and notifies all waiting processes.
    pub fn retain<F>(&mut self, predicate: F)
    where
        F: Fn(FileHandle) -> bool,
    {
        if !self.locking_handles.is_empty() {
            self.locking_handles.retain(|w| {
                if let Some(fh) = w.upgrade() {
                    predicate(fh)
                } else {
                    false
                }
            });
            if self.locking_handles.is_empty() {
                self.locked_exclusive = None;
                self.wait_queue.notify_all();
            }
        }
    }
}

/// st_blksize is measured in units of 512 bytes.
const DEFAULT_BYTES_PER_BLOCK: i64 = 512;

pub struct FlockOperation {
    operation: u32,
}

impl FlockOperation {
    pub fn from_flags(operation: u32) -> Result<Self, Errno> {
        if operation & !(LOCK_SH | LOCK_EX | LOCK_UN | LOCK_NB) != 0 {
            return error!(EINVAL);
        }
        if [LOCK_SH, LOCK_EX, LOCK_UN].iter().filter(|&&o| operation & o == o).count() != 1 {
            return error!(EINVAL);
        }
        Ok(Self { operation })
    }

    pub fn is_unlock(&self) -> bool {
        self.operation & LOCK_UN > 0
    }

    pub fn is_lock_exclusive(&self) -> bool {
        self.operation & LOCK_EX > 0
    }

    pub fn is_blocking(&self) -> bool {
        self.operation & LOCK_NB == 0
    }
}

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum UnlinkKind {
    /// Unlink a directory.
    Directory,

    /// Unlink a non-directory.
    NonDirectory,
}

pub enum SymlinkTarget {
    Path(FsString),
    Node(NamespaceNode),
}

#[derive(PartialEq, Eq)]
pub enum XattrOp {
    /// Set the value of the extended attribute regardless of whether it exists.
    Set,
    /// Create a new extended attribute. Fail if it already exists.
    Create,
    /// Replace the value of the extended attribute. Fail if it doesn't exist.
    Replace,
}

pub trait FsNodeOps: Send + Sync + AsAny + 'static {
    /// Build the `FileOps` for the file associated to this node.
    ///
    /// The returned FileOps will be used to create a FileObject, which might
    /// be assigned an FdNumber.
    fn create_file_ops(&self, node: &FsNode, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno>;

    /// Find an existing child node and populate the child parameter. Return the node.
    ///
    /// The child parameter is an empty node. Operations other than initialize may panic before
    /// initialize is called.
    fn lookup(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
        _name: &FsStr,
    ) -> Result<FsNodeHandle, Errno> {
        error!(ENOTDIR)
    }

    /// Create and return the given child node.
    ///
    /// The mode field of the FsNodeInfo indicates what kind of child to
    /// create.
    ///
    /// This function is never called with FileMode::IFDIR. The mkdir function
    /// is used to create directories instead.
    fn mknod(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _mode: FileMode,
        _dev: DeviceType,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(ENOTDIR)
    }

    /// Create and return the given child node as a subdirectory.
    fn mkdir(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _mode: FileMode,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(ENOTDIR)
    }

    /// Creates a symlink with the given `target` path.
    fn create_symlink(
        &self,
        _node: &FsNode,
        _name: &FsStr,
        _target: &FsStr,
        _owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        error!(ENOTDIR)
    }

    /// Reads the symlink from this node.
    fn readlink(
        &self,
        _node: &FsNode,
        _current_task: &CurrentTask,
    ) -> Result<SymlinkTarget, Errno> {
        error!(EINVAL)
    }

    /// Create a hard link with the given name to the given child.
    fn link(&self, _node: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        error!(EPERM)
    }

    /// Remove the child with the given name, if the child exists.
    ///
    /// The UnlinkKind parameter indicates whether the caller intends to unlink
    /// a directory or a non-directory child.
    fn unlink(&self, _node: &FsNode, _name: &FsStr, _child: &FsNodeHandle) -> Result<(), Errno> {
        error!(ENOTDIR)
    }

    /// Change the length of the file.
    fn truncate(&self, _node: &FsNode, _length: u64) -> Result<(), Errno> {
        error!(EINVAL)
    }

    /// Update node.info as needed.
    ///
    /// FsNode calls this method before converting the FsNodeInfo struct into
    /// the stat_t struct to give the file system a chance to update this data
    /// before it is used by clients.
    ///
    /// File systems that keep the FsNodeInfo up-to-date do not need to
    /// override this function.
    ///
    /// Return a reader lock on the updated information.
    fn update_info<'a>(&self, node: &'a FsNode) -> Result<RwLockReadGuard<'a, FsNodeInfo>, Errno> {
        Ok(node.info())
    }

    /// Get an extended attribute on the node.
    fn get_xattr(&self, _name: &FsStr) -> Result<FsString, Errno> {
        error!(ENOTSUP)
    }

    /// Set an extended attribute on the node.
    fn set_xattr(&self, _name: &FsStr, _value: &FsStr, _op: XattrOp) -> Result<(), Errno> {
        error!(ENOTSUP)
    }

    fn remove_xattr(&self, _name: &FsStr) -> Result<(), Errno> {
        error!(ENOTSUP)
    }

    fn list_xattrs(&self) -> Result<Vec<FsString>, Errno> {
        error!(ENOTSUP)
    }
}

/// Implements [`FsNodeOps`] methods in a way that makes sense for symlinks.
/// You must implement [`FsNodeOps::readlink`].
macro_rules! fs_node_impl_symlink {
    () => {
        fn create_file_ops(
            &self,
            _node: &crate::fs::FsNode,
            _flags: crate::types::OpenFlags,
        ) -> Result<Box<dyn crate::fs::FileOps>, crate::types::Errno> {
            unreachable!("Symlink nodes cannot be opened.");
        }
    };
}

macro_rules! fs_node_impl_dir_readonly {
    () => {
        fn mkdir(
            &self,
            _node: &crate::fs::FsNode,
            _name: &crate::fs::FsStr,
            _mode: crate::types::FileMode,
            _owner: crate::auth::FsCred,
        ) -> Result<crate::fs::FsNodeHandle, Errno> {
            error!(EROFS)
        }

        fn mknod(
            &self,
            _node: &crate::fs::FsNode,
            _name: &crate::fs::FsStr,
            _mode: crate::types::FileMode,
            _dev: crate::types::DeviceType,
            _owner: crate::auth::FsCred,
        ) -> Result<crate::fs::FsNodeHandle, Errno> {
            error!(EROFS)
        }

        fn create_symlink(
            &self,
            _node: &crate::fs::FsNode,
            _name: &crate::fs::FsStr,
            _target: &crate::fs::FsStr,
            _owner: crate::auth::FsCred,
        ) -> Result<crate::fs::FsNodeHandle, Errno> {
            error!(EROFS)
        }

        fn link(
            &self,
            _node: &crate::fs::FsNode,
            _name: &crate::fs::FsStr,
            _child: &crate::fs::FsNodeHandle,
        ) -> Result<(), Errno> {
            error!(EROFS)
        }

        fn unlink(
            &self,
            _node: &crate::fs::FsNode,
            _name: &crate::fs::FsStr,
            _child: &crate::fs::FsNodeHandle,
        ) -> Result<(), Errno> {
            error!(EROFS)
        }
    };
}

/// Implements [`FsNodeOps::set_xattr`] by delegating to another [`FsNodeOps`]
/// object.
macro_rules! fs_node_impl_xattr_delegate {
    ($self:ident, $delegate:expr) => {
        fn get_xattr(
            &$self,
            name: &crate::fs::FsStr,
        ) -> Result<FsString, crate::types::Errno> {
            $delegate.get_xattr(name)
        }

        fn set_xattr(
            &$self,
            name: &crate::fs::FsStr,
            value: &crate::fs::FsStr,
            op: crate::fs::XattrOp,
        ) -> Result<(), crate::types::Errno> {
            $delegate.set_xattr(name, value, op)
        }

        fn remove_xattr(
            &$self,
            name: &crate::fs::FsStr,
        ) -> Result<(), crate::types::Errno> {
            $delegate.remove_xattr(name)
        }

        fn list_xattrs(
            &$self,
        ) -> Result<Vec<crate::fs::FsString>, crate::types::Errno> {
            $delegate.list_xattrs()
        }
    };
    ($delegate:expr) => { fs_node_impl_xattr_delegate(self, $delegate) };
}

// Public re-export of macros allows them to be used like regular rust items.
pub(crate) use fs_node_impl_dir_readonly;
pub(crate) use fs_node_impl_symlink;
pub(crate) use fs_node_impl_xattr_delegate;

pub struct SpecialNode;

impl FsNodeOps for SpecialNode {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        unreachable!("Special nodes cannot be opened.");
    }
}

impl FsNode {
    pub fn new_root(ops: impl FsNodeOps) -> FsNode {
        Self::new_internal(Box::new(ops), Weak::new(), 1, mode!(IFDIR, 0o777), FsCred::root())
    }

    /// Create a node without inserting it into the FileSystem node cache. This is usually not what
    /// you want! Only use if you're also using get_or_create_node, like ext4.
    pub fn new_uncached(
        ops: Box<dyn FsNodeOps>,
        fs: &FileSystemHandle,
        inode_num: ino_t,
        mode: FileMode,
        owner: FsCred,
    ) -> FsNodeHandle {
        Arc::new(Self::new_internal(ops, Arc::downgrade(fs), inode_num, mode, owner))
    }

    fn new_internal(
        ops: Box<dyn FsNodeOps>,
        fs: Weak<FileSystem>,
        inode_num: ino_t,
        mode: FileMode,
        owner: FsCred,
    ) -> Self {
        let now = fuchsia_runtime::utc_time();
        let info = FsNodeInfo {
            mode,
            blksize: DEFAULT_BYTES_PER_BLOCK,
            uid: owner.uid,
            gid: owner.gid,
            link_count: if mode.is_dir() { 2 } else { 1 },
            time_create: now,
            time_access: now,
            time_modify: now,
            ..Default::default()
        };
        // The linter will fail in non test mode as it will not see the lock check.
        #[allow(clippy::let_and_return)]
        {
            let result = Self {
                ops,
                fs,
                inode_num,
                fifo: if mode.is_fifo() { Some(Pipe::new()) } else { None },
                socket: OnceCell::new(),
                info: RwLock::new(info),
                append_lock: Default::default(),
                flock_info: Default::default(),
                record_locks: Default::default(),
            };
            #[cfg(any(test, debug_assertions))]
            {
                let _l1 = result.append_lock.read();
                let _l2 = result.info.read();
                let _l3 = result.flock_info.lock();
            }
            result
        }
    }

    pub fn fs(&self) -> FileSystemHandle {
        self.fs.upgrade().expect("FileSystem did not live long enough")
    }

    pub fn set_fs(&mut self, fs: &FileSystemHandle) {
        self.fs = Arc::downgrade(fs);
    }

    fn ops(&self) -> &dyn FsNodeOps {
        self.ops.as_ref()
    }

    /// Returns the `FsNode`'s `FsNodeOps` as a `&T`, or `None` if the downcast fails.
    pub fn downcast_ops<T>(&self) -> Option<&T>
    where
        T: 'static,
    {
        self.ops().as_any().downcast_ref::<T>()
    }

    pub fn on_file_closed(&self) {
        let mut flock_info = self.flock_info.lock();
        flock_info.retain(|_| true);
    }

    /// Lock/Unlock the current node.
    ///
    /// See flock(2).
    pub fn flock(
        &self,
        current_task: &CurrentTask,
        file_handle: &FileHandle,
        operation: FlockOperation,
    ) -> Result<(), Errno> {
        loop {
            self.check_access(current_task, Access::READ)?;
            let mut flock_info = self.flock_info.lock();
            if operation.is_unlock() {
                flock_info.retain(|fh| !Arc::ptr_eq(&fh, file_handle));
                return Ok(());
            }
            // Operation is a locking operation.
            // 1. File is not locked
            if flock_info.locked_exclusive.is_none() {
                flock_info.locked_exclusive = Some(operation.is_lock_exclusive());
                flock_info.locking_handles.push(Arc::downgrade(file_handle));
                return Ok(());
            }

            let file_lock_is_exclusive = flock_info.locked_exclusive == Some(true);
            let fd_has_lock = flock_info
                .locking_handles
                .iter()
                .find_map(|w| {
                    w.upgrade().and_then(|fh| {
                        if Arc::ptr_eq(&fh, file_handle) {
                            Some(())
                        } else {
                            None
                        }
                    })
                })
                .is_some();

            // 2. File is locked, but fd already have a lock
            if fd_has_lock {
                if operation.is_lock_exclusive() == file_lock_is_exclusive {
                    // Correct lock is already held, return.
                    return Ok(());
                } else {
                    // Incorrect lock is held. Release the lock and loop back to try to reacquire
                    // it. flock doesn't guarantee atomic lock type switching.
                    flock_info.retain(|fh| !Arc::ptr_eq(&fh, file_handle));
                    continue;
                }
            }

            // 3. File is locked, and fd doesn't have a lock.
            if !file_lock_is_exclusive && !operation.is_lock_exclusive() {
                // The lock is not exclusive, let's grab it.
                flock_info.locking_handles.push(Arc::downgrade(file_handle));
                return Ok(());
            }

            // 4. The operation cannot be done at this time.
            if !operation.is_blocking() {
                return error!(EWOULDBLOCK);
            }

            // Register a waiter to be notified when the lock is released. Release the lock on
            // FlockInfo, and wait.
            let waiter = Waiter::new();
            flock_info.wait_queue.wait_async(&waiter);
            std::mem::drop(flock_info);
            waiter.wait(current_task)?;
        }
    }

    pub fn record_lock(
        &self,
        current_task: &CurrentTask,
        file: &FileObject,
        cmd: u32,
        flock: uapi::flock,
    ) -> Result<Option<uapi::flock>, Errno> {
        self.record_locks.lock(current_task, file, cmd, flock)
    }

    /// Release all record locks acquired by the given process.
    pub fn record_lock_release(&self, fd_table_id: FdTableId) {
        self.record_locks.release_locks(fd_table_id);
    }

    pub fn create_file_ops(&self, flags: OpenFlags) -> Result<Box<dyn FileOps>, Errno> {
        self.ops().create_file_ops(self, flags)
    }

    pub fn open(
        &self,
        current_task: &CurrentTask,
        flags: OpenFlags,
        check_access: bool,
    ) -> Result<Box<dyn FileOps>, Errno> {
        // If O_PATH is set, there is no need to create a real FileOps because
        // most file operations are disabled.
        if flags.contains(OpenFlags::PATH) {
            return Ok(Box::new(OPathOps::new()));
        }

        if check_access {
            self.check_access(current_task, Access::from_open_flags(flags))?;
        }

        let (mode, rdev) = {
            // Don't hold the info lock while calling into open_device or self.ops().
            // TODO: The mode and rdev are immutable and shouldn't require a lock to read.
            let info = self.info();
            (info.mode, info.rdev)
        };

        match mode & FileMode::IFMT {
            FileMode::IFCHR => {
                current_task.kernel().open_device(current_task, self, flags, rdev, DeviceMode::Char)
            }
            FileMode::IFBLK => current_task.kernel().open_device(
                current_task,
                self,
                flags,
                rdev,
                DeviceMode::Block,
            ),
            FileMode::IFIFO => Ok(Pipe::open(self.fifo.as_ref().unwrap(), flags)),
            // UNIX domain sockets can't be opened.
            FileMode::IFSOCK => error!(ENXIO),
            _ => self.create_file_ops(flags),
        }
    }

    pub fn lookup(&self, current_task: &CurrentTask, name: &FsStr) -> Result<FsNodeHandle, Errno> {
        self.check_access(current_task, Access::EXEC)?;
        self.ops().lookup(self, current_task, name)
    }

    pub fn mknod(
        &self,
        current_task: &CurrentTask,
        name: &FsStr,
        mode: FileMode,
        dev: DeviceType,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        assert!(mode & FileMode::IFMT != FileMode::EMPTY, "mknod called without node type.");
        self.check_access(current_task, Access::WRITE)?;
        self.ops().mknod(self, name, mode, dev, owner)
    }

    pub fn mkdir(
        &self,
        current_task: &CurrentTask,
        name: &FsStr,
        mode: FileMode,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        assert!(
            mode & FileMode::IFMT == FileMode::IFDIR,
            "mkdir called without directory node type."
        );
        self.check_access(current_task, Access::WRITE)?;
        self.ops().mkdir(self, name, mode, owner)
    }

    pub fn create_symlink(
        &self,
        current_task: &CurrentTask,
        name: &FsStr,
        target: &FsStr,
        owner: FsCred,
    ) -> Result<FsNodeHandle, Errno> {
        self.check_access(current_task, Access::WRITE)?;
        self.ops().create_symlink(self, name, target, owner)
    }

    pub fn readlink(&self, current_task: &CurrentTask) -> Result<SymlinkTarget, Errno> {
        // TODO(qsr): Is there a permission check here?
        let now = fuchsia_runtime::utc_time();
        self.info_write().time_access = now;
        self.ops().readlink(self, current_task)
    }

    pub fn link(&self, task: &Task, name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        self.check_access(task, Access::WRITE)?;
        self.ops().link(self, name, child)
    }

    pub fn unlink(&self, task: &Task, name: &FsStr, child: &FsNodeHandle) -> Result<(), Errno> {
        self.check_access(task, Access::WRITE)?;
        self.ops().unlink(self, name, child)
    }

    pub fn truncate(&self, task: &Task, length: u64) -> Result<(), Errno> {
        self.check_access(task, Access::WRITE)?;
        self.ops().truncate(self, length)
    }

    /// Check whether the node can be accessed in the current context with the specified access
    /// flags (read, write, or exec). Accounts for capabilities and whether the current user is the
    /// owner or is in the file's group.
    pub fn check_access(&self, task: &Task, access: Access) -> Result<(), Errno> {
        let (node_uid, node_gid, mode) = {
            let info = self.info();
            (info.uid, info.gid, info.mode.bits())
        };
        let creds = task.creds();
        if creds.has_capability(CAP_DAC_OVERRIDE) {
            return Ok(());
        }
        let mode_flags = if creds.euid == node_uid {
            (mode & 0o700) >> 6
        } else if creds.is_in_group(node_gid) {
            (mode & 0o070) >> 3
        } else {
            mode & 0o007
        };
        if (mode_flags & access.bits()) != access.bits() {
            return error!(EACCES);
        }
        Ok(())
    }

    /// Associates the provided socket with this file node.
    ///
    /// `set_socket` must be called before it is possible to look up `self`, since user space should
    ///  not be able to look up this node and find the socket missing.
    ///
    /// Note that it is a fatal error to call this method if a socket has already been bound for
    /// this node.
    ///
    /// # Parameters
    /// - `socket`: The socket to store in this file node.
    pub fn set_socket(&self, socket: SocketHandle) {
        assert!(self.socket.set(socket).is_ok());
    }

    /// Returns the socket associated with this node, if such a socket exists.
    pub fn socket(&self) -> Option<&SocketHandle> {
        self.socket.get()
    }

    /// Set the permissions on this FsNode to the given values.
    ///
    /// Does not change the IFMT of the node.
    pub fn chmod(&self, mode: FileMode) {
        // TODO(qsr, security): Check permissions.
        let mut info = self.info_write();
        info.mode = (info.mode & !FileMode::PERMISSIONS) | (mode & FileMode::PERMISSIONS);
    }

    /// Sets the owner and/or group on this FsNode.
    pub fn chown(&self, owner: Option<uid_t>, group: Option<gid_t>) {
        // TODO(qsr, security): Check permissions.
        let mut info = self.info_write();
        if let Some(owner) = owner {
            info.uid = owner;
        }
        if let Some(group) = group {
            info.gid = group;
        }
    }

    /// Whether this node is a directory.
    pub fn is_dir(&self) -> bool {
        self.info().mode.is_dir()
    }

    /// Whether this node is a socket.
    pub fn is_sock(&self) -> bool {
        self.info().mode.is_sock()
    }

    /// Whether this node is a symbolic link.
    pub fn is_lnk(&self) -> bool {
        self.info().mode.is_lnk()
    }

    /// Update the access and modify time for this node to now.
    pub fn touch(&self) {
        let now = fuchsia_runtime::utc_time();
        let mut info = self.info_write();
        info.time_access = now;
        info.time_modify = now;
    }

    pub fn stat(&self) -> Result<stat_t, Errno> {
        let info = self.ops().update_info(self)?;
        Ok(stat_t {
            st_ino: self.inode_num,
            st_mode: info.mode.bits(),
            st_size: info.size as off_t,
            st_blocks: info.storage_size as i64 / info.blksize,
            st_nlink: info.link_count,
            st_uid: info.uid,
            st_gid: info.gid,
            st_ctim: timespec_from_time(info.time_create),
            st_mtim: timespec_from_time(info.time_modify),
            st_atim: timespec_from_time(info.time_access),
            st_dev: self.fs().dev_id.bits(),
            st_rdev: info.rdev.bits(),
            st_blksize: info.blksize,
            ..Default::default()
        })
    }

    /// Check that `task` can access the extended attributed `name`. Will return the result of
    /// `error` in case the attributed is trusted and the task has not the CAP_SYS_ADMIN
    /// capability.
    fn check_trusted_attribute_access<F>(task: &Task, name: &FsStr, error: F) -> Result<(), Errno>
    where
        F: FnOnce() -> Errno,
    {
        if task.creds().has_capability(CAP_SYS_ADMIN)
            || !name.starts_with(XATTR_TRUSTED_PREFIX.to_bytes())
        {
            Ok(())
        } else {
            Err(error())
        }
    }

    pub fn get_xattr(&self, task: &Task, name: &FsStr) -> Result<FsString, Errno> {
        self.check_access(task, Access::READ)?;
        Self::check_trusted_attribute_access(task, name, || errno!(ENODATA))?;
        self.ops().get_xattr(name)
    }

    pub fn set_xattr(
        &self,
        task: &Task,
        name: &FsStr,
        value: &FsStr,
        op: XattrOp,
    ) -> Result<(), Errno> {
        self.check_access(task, Access::WRITE)?;
        Self::check_trusted_attribute_access(task, name, || errno!(EPERM))?;
        self.ops().set_xattr(name, value, op)
    }

    pub fn remove_xattr(&self, task: &Task, name: &FsStr) -> Result<(), Errno> {
        self.check_access(task, Access::WRITE)?;
        Self::check_trusted_attribute_access(task, name, || errno!(EPERM))?;
        self.ops().remove_xattr(name)
    }

    pub fn list_xattrs(&self, task: &Task) -> Result<Vec<FsString>, Errno> {
        Ok(self
            .ops()
            .list_xattrs()?
            .into_iter()
            .filter(|name| {
                Self::check_trusted_attribute_access(task, name, || errno!(EPERM)).is_ok()
            })
            .collect())
    }

    pub fn info(&self) -> RwLockReadGuard<'_, FsNodeInfo> {
        self.info.read()
    }
    pub fn info_write(&self) -> RwLockWriteGuard<'_, FsNodeInfo> {
        self.info.write()
    }
}

impl Drop for FsNode {
    fn drop(&mut self) {
        if let Some(fs) = self.fs.upgrade() {
            fs.remove_node(self);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::auth::Credentials;
    use crate::testing::*;

    #[::fuchsia::test]
    fn open_device_file() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Create a device file that points to the `zero` device (which is automatically
        // registered in the kernel).
        current_task
            .fs()
            .root()
            .create_node(&current_task, b"zero", mode!(IFCHR, 0o666), DeviceType::ZERO)
            .expect("create_node");

        // Prepare the user buffer with some values other than the expected content (non-zero).
        const CONTENT_LEN: usize = 10;
        let address = map_memory(&current_task, UserAddress::default(), CONTENT_LEN as u64);
        current_task.mm.write_memory(address, &[0xff; CONTENT_LEN]).expect("write memory");

        // Read from the zero device.
        let device_file =
            current_task.open_file(b"zero", OpenFlags::RDONLY).expect("open device file");
        device_file
            .read(&current_task, &[UserBuffer { address, length: CONTENT_LEN }])
            .expect("read from zero");

        // Assert the contents.
        let content = &mut [0xff; CONTENT_LEN];
        current_task.mm.read_memory(address, content).expect("read memory");
        assert_eq!(&[0; CONTENT_LEN], content);
    }

    #[::fuchsia::test]
    fn node_info_is_reflected_in_stat() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Create a node.
        let node = &current_task
            .fs()
            .root()
            .create_node(&current_task, b"zero", FileMode::IFCHR, DeviceType::ZERO)
            .expect("create_node")
            .entry
            .node;
        {
            let mut info = node.info_write();
            info.mode = FileMode::IFSOCK;
            info.size = 1;
            info.storage_size = 8;
            info.blksize = 4;
            info.uid = 9;
            info.gid = 10;
            info.link_count = 11;
            info.time_create = zx::Time::from_nanos(1);
            info.time_access = zx::Time::from_nanos(2);
            info.time_modify = zx::Time::from_nanos(3);
            info.rdev = DeviceType::new(13, 13);
        }
        let stat = node.stat().expect("stat");

        assert_eq!(stat.st_mode, FileMode::IFSOCK.bits());
        assert_eq!(stat.st_size, 1);
        assert_eq!(stat.st_blksize, 4);
        assert_eq!(stat.st_blocks, 2);
        assert_eq!(stat.st_uid, 9);
        assert_eq!(stat.st_gid, 10);
        assert_eq!(stat.st_nlink, 11);
        assert_eq!(time_from_timespec(stat.st_ctim).expect("ctim"), zx::Time::from_nanos(1));
        assert_eq!(time_from_timespec(stat.st_atim).expect("atim"), zx::Time::from_nanos(2));
        assert_eq!(time_from_timespec(stat.st_mtim).expect("mtim"), zx::Time::from_nanos(3));
        assert_eq!(stat.st_rdev, DeviceType::new(13, 13).bits());
    }

    #[::fuchsia::test]
    fn test_flock_operation() {
        assert!(FlockOperation::from_flags(0).is_err());
        assert!(FlockOperation::from_flags(u32::MAX).is_err());

        let operation1 = FlockOperation::from_flags(LOCK_SH).expect("from_flags");
        assert!(!operation1.is_unlock());
        assert!(!operation1.is_lock_exclusive());
        assert!(operation1.is_blocking());

        let operation2 = FlockOperation::from_flags(LOCK_EX | LOCK_NB).expect("from_flags");
        assert!(!operation2.is_unlock());
        assert!(operation2.is_lock_exclusive());
        assert!(!operation2.is_blocking());

        let operation3 = FlockOperation::from_flags(LOCK_UN).expect("from_flags");
        assert!(operation3.is_unlock());
        assert!(!operation3.is_lock_exclusive());
        assert!(operation3.is_blocking());
    }

    #[::fuchsia::test]
    fn test_check_access() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mut creds = Credentials::with_ids(1, 2);
        creds.groups = vec![3, 4];
        current_task.set_creds(creds);

        // Create a node.
        let node = &current_task
            .fs()
            .root()
            .create_node(&current_task, b"foo", FileMode::IFREG, DeviceType::NONE)
            .expect("create_node")
            .entry
            .node;
        let check_access = |uid: uid_t, gid: gid_t, perm: u32, access: Access| {
            {
                let mut info = node.info_write();
                info.mode = mode!(IFREG, perm);
                info.uid = uid;
                info.gid = gid;
            }
            node.check_access(&current_task, access)
        };

        assert_eq!(check_access(0, 0, 0o700, Access::EXEC), error!(EACCES));
        assert_eq!(check_access(0, 0, 0o700, Access::READ), error!(EACCES));
        assert_eq!(check_access(0, 0, 0o700, Access::WRITE), error!(EACCES));

        assert_eq!(check_access(0, 0, 0o070, Access::EXEC), error!(EACCES));
        assert_eq!(check_access(0, 0, 0o070, Access::READ), error!(EACCES));
        assert_eq!(check_access(0, 0, 0o070, Access::WRITE), error!(EACCES));

        assert_eq!(check_access(0, 0, 0o007, Access::EXEC), Ok(()));
        assert_eq!(check_access(0, 0, 0o007, Access::READ), Ok(()));
        assert_eq!(check_access(0, 0, 0o007, Access::WRITE), Ok(()));

        assert_eq!(check_access(1, 0, 0o700, Access::EXEC), Ok(()));
        assert_eq!(check_access(1, 0, 0o700, Access::READ), Ok(()));
        assert_eq!(check_access(1, 0, 0o700, Access::WRITE), Ok(()));

        assert_eq!(check_access(1, 0, 0o100, Access::EXEC), Ok(()));
        assert_eq!(check_access(1, 0, 0o100, Access::READ), error!(EACCES));
        assert_eq!(check_access(1, 0, 0o100, Access::WRITE), error!(EACCES));

        assert_eq!(check_access(1, 0, 0o200, Access::EXEC), error!(EACCES));
        assert_eq!(check_access(1, 0, 0o200, Access::READ), error!(EACCES));
        assert_eq!(check_access(1, 0, 0o200, Access::WRITE), Ok(()));

        assert_eq!(check_access(1, 0, 0o400, Access::EXEC), error!(EACCES));
        assert_eq!(check_access(1, 0, 0o400, Access::READ), Ok(()));
        assert_eq!(check_access(1, 0, 0o400, Access::WRITE), error!(EACCES));

        assert_eq!(check_access(0, 2, 0o700, Access::EXEC), error!(EACCES));
        assert_eq!(check_access(0, 2, 0o700, Access::READ), error!(EACCES));
        assert_eq!(check_access(0, 2, 0o700, Access::WRITE), error!(EACCES));

        assert_eq!(check_access(0, 2, 0o070, Access::EXEC), Ok(()));
        assert_eq!(check_access(0, 2, 0o070, Access::READ), Ok(()));
        assert_eq!(check_access(0, 2, 0o070, Access::WRITE), Ok(()));

        assert_eq!(check_access(0, 3, 0o070, Access::EXEC), Ok(()));
        assert_eq!(check_access(0, 3, 0o070, Access::READ), Ok(()));
        assert_eq!(check_access(0, 3, 0o070, Access::WRITE), Ok(()));
    }
}
