// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use fuchsia_zircon as zx;
use std::fmt;
use std::sync::Arc;

use crate::fs::*;
use crate::lock::Mutex;
use crate::logging::{impossible_error, not_implemented};
use crate::mm::{DesiredAddress, MappedVmo, MappingOptions};
use crate::syscalls::*;
use crate::task::*;
use crate::types::as_any::*;
use crate::types::*;

pub const MAX_LFS_FILESIZE: usize = 0x7fffffffffffffff;

bitflags! {
    pub struct WaitAsyncOptions: u32 {
        // Ignore events active at the time of the call to wait_async().
        const EDGE_TRIGGERED = 1;
    }
}

pub enum SeekOrigin {
    Set,
    Cur,
    End,
}

impl SeekOrigin {
    pub fn from_raw(whence: u32) -> Result<SeekOrigin, Errno> {
        match whence {
            SEEK_SET => Ok(SeekOrigin::Set),
            SEEK_CUR => Ok(SeekOrigin::Cur),
            SEEK_END => Ok(SeekOrigin::End),
            _ => error!(EINVAL),
        }
    }
}

pub enum BlockableOpsResult<T> {
    Done(T),
    Partial(T),
}

impl<T> BlockableOpsResult<T> {
    pub fn value(self) -> T {
        match self {
            Self::Done(v) | Self::Partial(v) => v,
        }
    }

    pub fn is_partial(&self) -> bool {
        match self {
            Self::Done(_) => false,
            Self::Partial(_) => true,
        }
    }
}

/// Corresponds to struct file_operations in Linux, plus any filesystem-specific data.
pub trait FileOps: Send + Sync + AsAny + 'static {
    /// Called when the FileObject is closed.
    fn close(&self, _file: &FileObject) {}

    /// Read from the file without an offset. If your file is seekable, consider implementing this
    /// with [`fileops_impl_seekable`].
    fn read(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno>;
    /// Read from the file at an offset. If your file is seekable, consider implementing this with
    /// [`fileops_impl_nonseekable`].
    fn read_at(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno>;
    /// Write to the file without an offset. If your file is seekable, consider implementing this
    /// with [`fileops_impl_seekable`].
    fn write(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        data: &[UserBuffer],
    ) -> Result<usize, Errno>;
    /// Write to the file at a offset. If your file is nonseekable, consider implementing this with
    /// [`fileops_impl_nonseekable`].
    fn write_at(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno>;

    /// Adjust the seek offset if the file is seekable.
    fn seek(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno>;

    /// Returns a VMO representing this file. At least the requested protection flags must
    /// be set on the VMO. Reading or writing the VMO must read or write the file. If this is not
    /// possible given the requested protection, an error must be returned.
    /// The `length` is a hint for the desired size of the VMO. The returned VMO may be larger or
    /// smaller than the requested length.
    /// This method is typically called by [`Self::mmap`].
    fn get_vmo(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _length: Option<usize>,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        error!(ENODEV)
    }

    /// Responds to an mmap call. The default implementation calls [`Self::get_vmo`] to get a VMO
    /// and then maps it with [`crate::mm::MemoryManager::map`].
    /// Only implement this trait method if your file needs to control mapping, or record where
    /// a VMO gets mapped.
    fn mmap(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        addr: DesiredAddress,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        // Sanitize the protection flags to only include PERM_READ, PERM_WRITE, and PERM_EXECUTE.
        let zx_prot_flags = flags
            & (zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE | zx::VmarFlags::PERM_EXECUTE);

        let vmo = Arc::new(if options.contains(MappingOptions::SHARED) {
            self.get_vmo(file, current_task, Some(length), zx_prot_flags)?
        } else {
            // TODO(tbodt): Use PRIVATE_CLONE to have the filesystem server do the clone for us.
            let vmo = self.get_vmo(
                file,
                current_task,
                Some(length),
                zx_prot_flags - zx::VmarFlags::PERM_WRITE,
            )?;
            let mut clone_flags = zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE;
            if !zx_prot_flags.contains(zx::VmarFlags::PERM_WRITE) {
                clone_flags |= zx::VmoChildOptions::NO_WRITE;
            }
            vmo.create_child(clone_flags, 0, vmo.get_size().map_err(impossible_error)?)
                .map_err(impossible_error)?
        });
        let addr = current_task.mm.map(
            addr,
            vmo.clone(),
            vmo_offset,
            length,
            flags,
            options,
            Some(filename),
        )?;
        Ok(MappedVmo::new(vmo, addr))
    }

    /// Respond to a `getdents` or `getdents64` calls.
    ///
    /// The `file.offset` lock will be held while entering this method. The implementation must look
    /// at `sink.offset()` to read the current offset into the file.
    fn readdir(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        error!(ENOTDIR)
    }

    /// Establish a one-shot, asynchronous wait for the given FdEvents for the given file and task.
    /// If no options are set and the events are already active at the time of calling, handler
    /// will be called on immediately on the next wait. If `WaitAsyncOptions::EDGE_TRIGGERED` is
    /// specified as an option, active events are not considered.
    ///
    /// If your file does not block, implement this with fileops_impl_nonblocking.
    fn wait_async(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _waiter: &Waiter,
        _events: FdEvents,
        _handler: EventHandler,
        _options: WaitAsyncOptions,
    ) -> WaitKey;

    /// Cancel a wait set up by wait_async.
    /// Returns true if the wait has not been activated and has been cancelled.
    ///
    /// If your file does not block, implement this with fileops_impl_nonblocking.
    fn cancel_wait(&self, _current_task: &CurrentTask, _waiter: &Waiter, _key: WaitKey);

    fn query_events(&self, current_task: &CurrentTask) -> FdEvents;

    fn ioctl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        request: u32,
        _user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        default_ioctl(current_task, request)
    }

    fn fcntl(
        &self,
        file: &FileObject,
        current_task: &CurrentTask,
        cmd: u32,
        arg: u64,
    ) -> Result<SyscallResult, Errno> {
        default_fcntl(current_task, file, cmd, arg)
    }
}

/// Implements [`FileOps`] methods in a way that makes sense for non-seekable files.
/// You must implement [`FileOps::read`] and [`FileOps::write`].
macro_rules! fileops_impl_nonseekable {
    () => {
        fn read_at(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _offset: usize,
            _data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            use crate::types::errno::*;
            error!(ESPIPE)
        }
        fn write_at(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _offset: usize,
            _data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            use crate::types::errno::*;
            error!(ESPIPE)
        }
        fn seek(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _offset: crate::types::off_t,
            _whence: crate::fs::SeekOrigin,
        ) -> Result<crate::types::off_t, crate::types::Errno> {
            use crate::types::errno::*;
            error!(ESPIPE)
        }
    };
}

/// Implements [`FileOps`] methods in a way that makes sense for seekable files.
/// You must implement [`FileOps::read_at`] and [`FileOps::write_at`].
macro_rules! fileops_impl_seekable {
    () => {
        fn read(
            &self,
            file: &crate::fs::FileObject,
            current_task: &crate::task::CurrentTask,
            data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            let mut offset = file.offset.lock();
            let size = self.read_at(file, current_task, *offset as usize, data)?;
            *offset += size as crate::types::off_t;
            Ok(size)
        }
        fn write(
            &self,
            file: &crate::fs::FileObject,
            current_task: &crate::task::CurrentTask,
            data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            let mut offset = file.offset.lock();
            if file.flags().contains(OpenFlags::APPEND) {
                *offset = file.node().info().size as crate::types::off_t;
            }
            let size = self.write_at(file, current_task, *offset as usize, data)?;
            *offset += size as crate::types::off_t;
            Ok(size)
        }
        fn seek(
            &self,
            file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            offset: crate::types::off_t,
            whence: crate::fs::SeekOrigin,
        ) -> Result<crate::types::off_t, crate::types::Errno> {
            use crate::types::errno::*;
            let mut current_offset = file.offset.lock();
            let new_offset = match whence {
                crate::fs::SeekOrigin::Set => Some(offset),
                crate::fs::SeekOrigin::Cur => (*current_offset).checked_add(offset),
                crate::fs::SeekOrigin::End => {
                    let stat = file.node().stat()?;
                    offset.checked_add(stat.st_size as crate::types::off_t)
                }
            }
            .ok_or_else(|| errno!(EINVAL))?;

            if new_offset < 0 {
                return error!(EINVAL);
            }

            *current_offset = new_offset;
            Ok(*current_offset)
        }
    };
}

/// Implements [`FileOps`] methods in a way that makes sense for files that ignore
/// seeking operations and always read/write at offset 0.
/// You must implement [`FileOps::read_at`] and [`FileOps::write_at`].
macro_rules! fileops_impl_seekless {
    () => {
        fn read(
            &self,
            file: &crate::fs::FileObject,
            current_task: &crate::task::CurrentTask,
            data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            self.read_at(file, current_task, 0, data)
        }
        fn write(
            &self,
            file: &crate::fs::FileObject,
            current_task: &crate::task::CurrentTask,
            data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            self.write_at(file, current_task, 0, data)
        }
        fn seek(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _offset: crate::types::off_t,
            _whence: crate::fs::SeekOrigin,
        ) -> Result<crate::types::off_t, crate::types::Errno> {
            Ok(0)
        }
    };
}

/// Implements [`FileOps`] methods in a way that makes sense for directories. You must implement
/// [`FileOps::seek`] and [`FileOps::readdir`].
macro_rules! fileops_impl_directory {
    () => {
        crate::fs::fileops_impl_nonblocking!();

        fn read(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            use crate::types::errno::*;
            error!(EISDIR)
        }

        fn read_at(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _offset: usize,
            _data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            use crate::types::errno::*;
            error!(EISDIR)
        }

        fn write(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            use crate::types::errno::*;
            error!(EISDIR)
        }

        fn write_at(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _offset: usize,
            _data: &[crate::types::UserBuffer],
        ) -> Result<usize, crate::types::Errno> {
            use crate::types::errno::*;
            error!(EISDIR)
        }
    };
}

/// Implements [`FileOps`] methods in a way that makes sense for files that never block
/// while reading/writing. The [`FileOps::wait_async`] and [`FileOps::query_events`] methods are
/// implemented for you.
macro_rules! fileops_impl_nonblocking {
    () => {
        fn wait_async(
            &self,
            _file: &crate::fs::FileObject,
            _current_task: &crate::task::CurrentTask,
            _waiter: &crate::task::Waiter,
            _events: crate::fs::FdEvents,
            _handler: crate::task::EventHandler,
            _options: crate::fs::WaitAsyncOptions,
        ) -> crate::task::WaitKey {
            crate::task::WaitKey::empty()
        }

        fn cancel_wait(
            &self,
            _current_task: &CurrentTask,
            _waiter: &crate::task::Waiter,
            _key: crate::task::WaitKey,
        ) {
        }

        fn query_events(&self, _current_task: &crate::task::CurrentTask) -> crate::fs::FdEvents {
            crate::fs::FdEvents::POLLIN | crate::fs::FdEvents::POLLOUT
        }
    };
}

// Public re-export of macros allows them to be used like regular rust items.

pub(crate) use fileops_impl_directory;
pub(crate) use fileops_impl_nonblocking;
pub(crate) use fileops_impl_nonseekable;
pub(crate) use fileops_impl_seekable;
pub(crate) use fileops_impl_seekless;

pub fn default_ioctl(current_task: &CurrentTask, request: u32) -> Result<SyscallResult, Errno> {
    not_implemented!(current_task, "ioctl: request=0x{:x}", request);
    error!(ENOTTY)
}

pub fn default_fcntl(
    current_task: &CurrentTask,
    file: &FileObject,
    cmd: u32,
    arg: u64,
) -> Result<SyscallResult, Errno> {
    match cmd {
        F_SETLK | F_SETLKW | F_GETLK => {
            let flock_ref = UserRef::<uapi::flock>::new(arg.into());
            let flock = current_task.mm.read_object(flock_ref)?;
            if let Some(flock) = file.record_lock(current_task, cmd, flock)? {
                current_task.mm.write_object(flock_ref, &flock)?;
            }
            Ok(SUCCESS)
        }
        _ => {
            not_implemented!(current_task, "fcntl: command={} not implemented", cmd);
            error!(EINVAL)
        }
    }
}

pub struct OPathOps {}

impl OPathOps {
    pub fn new() -> OPathOps {
        OPathOps {}
    }
}

impl FileOps for OPathOps {
    fileops_impl_nonblocking!();

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EBADF)
    }
    fn read_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EBADF)
    }
    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EBADF)
    }
    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        error!(EBADF)
    }
    fn seek(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: off_t,
        _whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        error!(EBADF)
    }
    fn get_vmo(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _length: Option<usize>,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        error!(EBADF)
    }
    fn readdir(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        error!(EBADF)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _request: u32,
        _user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        error!(EBADF)
    }

    fn fcntl(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        cmd: u32,
        _arg: u64,
    ) -> Result<SyscallResult, Errno> {
        match cmd {
            F_SETLK | F_SETLKW | F_GETLK => {
                error!(EBADF)
            }
            _ => {
                // Note: this can be a valid operation for files opened with O_PATH.
                not_implemented!(current_task, "fcntl: command={} not implemented", cmd);
                error!(EINVAL)
            }
        }
    }
}

/// A session with a file object.
///
/// Each time a client calls open(), we create a new FileObject from the
/// underlying FsNode that receives the open(). This object contains the state
/// that is specific to this sessions whereas the underlying FsNode contains
/// the state that is shared between all the sessions.
pub struct FileObject {
    ops: Box<dyn FileOps>,

    /// The NamespaceNode associated with this FileObject.
    ///
    /// Represents the name the process used to open this file.
    pub name: NamespaceNode,

    pub fs: FileSystemHandle,

    pub offset: Mutex<off_t>,

    flags: Mutex<OpenFlags>,

    async_owner: Mutex<pid_t>,
}

pub type FileHandle = Arc<FileObject>;

impl FileObject {
    /// Create a FileObject that is not mounted in a namespace.
    ///
    /// In particular, this will create a new unrooted entries. This should not be used on
    /// file system with persistent entries, as the created entry will be out of sync with the one
    /// from the file system.
    ///
    /// The returned FileObject does not have a name.
    pub fn new_anonymous(
        ops: Box<dyn FileOps>,
        node: FsNodeHandle,
        flags: OpenFlags,
    ) -> FileHandle {
        assert!(!node.fs().permanent_entries);
        Self::new(ops, NamespaceNode::new_anonymous(DirEntry::new_unrooted(node)), flags)
    }

    /// Create a FileObject with an associated NamespaceNode.
    ///
    /// This function is not typically called directly. Instead, consider
    /// calling NamespaceNode::open.
    pub fn new(ops: Box<dyn FileOps>, name: NamespaceNode, flags: OpenFlags) -> FileHandle {
        let fs = name.entry.node.fs();
        Arc::new(Self {
            name,
            fs,
            ops,
            offset: Mutex::new(0),
            flags: Mutex::new(flags),
            async_owner: Mutex::new(0),
        })
    }

    /// The FsNode from which this FileObject was created.
    pub fn node(&self) -> &FsNodeHandle {
        &self.name.entry.node
    }

    pub fn can_read(&self) -> bool {
        // TODO: Consider caching the access mode outside of this lock
        // because it cannot change.
        self.flags.lock().can_read()
    }

    pub fn can_write(&self) -> bool {
        // TODO: Consider caching the access mode outside of this lock
        // because it cannot change.
        self.flags.lock().can_write()
    }

    fn ops(&self) -> &dyn FileOps {
        self.ops.as_ref()
    }

    /// Returns the `FileObject`'s `FileOps` as a `&T`, or `None` if the downcast fails.
    ///
    /// This is useful for syscalls that only operate on a certain type of file.
    pub fn downcast_file<T>(&self) -> Option<&T>
    where
        T: 'static,
    {
        self.ops().as_any().downcast_ref::<T>()
    }

    pub fn blocking_op<T, Op>(
        &self,
        current_task: &CurrentTask,
        mut op: Op,
        events: FdEvents,
        deadline: Option<zx::Time>,
    ) -> Result<T, Errno>
    where
        Op: FnMut() -> Result<BlockableOpsResult<T>, Errno>,
    {
        let is_partial = |result: &Result<BlockableOpsResult<T>, Errno>| match result {
            Err(e) if e.code == EAGAIN => true,
            Ok(v) => v.is_partial(),
            _ => false,
        };

        // Run the operation a first time without registering a waiter in case no wait is needed.
        let result = op();
        if self.flags().contains(OpenFlags::NONBLOCK) || !is_partial(&result) {
            return result.map(BlockableOpsResult::value);
        }

        let waiter = Waiter::new();
        loop {
            // Register the waiter before running the operation to prevent a race.
            self.ops().wait_async(
                self,
                current_task,
                &waiter,
                events,
                WaitCallback::none(),
                WaitAsyncOptions::empty(),
            );
            let result = op();
            if !is_partial(&result) {
                return result.map(BlockableOpsResult::value);
            }
            waiter.wait_until(current_task, deadline.unwrap_or(zx::Time::INFINITE)).map_err(
                |e| {
                    if e == ETIMEDOUT {
                        errno!(EAGAIN)
                    } else {
                        e
                    }
                },
            )?;
        }
    }

    pub fn read(&self, current_task: &CurrentTask, data: &[UserBuffer]) -> Result<usize, Errno> {
        if !self.can_read() {
            return error!(EBADF);
        }
        self.blocking_op(
            current_task,
            || self.ops().read(self, current_task, data).map(BlockableOpsResult::Done),
            FdEvents::POLLIN | FdEvents::POLLHUP,
            None,
        )
    }

    pub fn read_at(
        &self,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if !self.can_read() {
            return error!(EBADF);
        }
        self.blocking_op(
            current_task,
            || self.ops().read_at(self, current_task, offset, data).map(BlockableOpsResult::Done),
            FdEvents::POLLIN | FdEvents::POLLHUP,
            None,
        )
    }

    pub fn write(&self, current_task: &CurrentTask, data: &[UserBuffer]) -> Result<usize, Errno> {
        if !self.can_write() {
            return error!(EBADF);
        }
        self.blocking_op(
            current_task,
            || {
                if self.flags().contains(OpenFlags::APPEND) {
                    let _guard = self.node().append_lock.write();
                    self.ops().write(self, current_task, data)
                } else {
                    let _guard = self.node().append_lock.read();
                    self.ops().write(self, current_task, data)
                }
                .map(BlockableOpsResult::Done)
            },
            FdEvents::POLLOUT | FdEvents::POLLHUP,
            None,
        )
    }

    pub fn write_at(
        &self,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if !self.can_write() {
            return error!(EBADF);
        }
        self.blocking_op(
            current_task,
            || {
                let _guard = self.node().append_lock.read();
                self.ops().write_at(self, current_task, offset, data).map(BlockableOpsResult::Done)
            },
            FdEvents::POLLOUT | FdEvents::POLLHUP,
            None,
        )
    }

    pub fn seek(
        &self,
        current_task: &CurrentTask,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        self.ops().seek(self, current_task, offset, whence)
    }

    pub fn get_vmo(
        &self,
        current_task: &CurrentTask,
        length: Option<usize>,
        prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        if prot.contains(zx::VmarFlags::PERM_READ) && !self.can_read() {
            return error!(EACCES);
        }
        if prot.contains(zx::VmarFlags::PERM_WRITE) && !self.can_write() {
            return error!(EACCES);
        }
        // TODO: Check for PERM_EXECUTE by checking whether the filesystem is mounted as noexec.
        self.ops().get_vmo(self, current_task, length, prot)
    }

    pub fn mmap(
        &self,
        current_task: &CurrentTask,
        addr: DesiredAddress,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        options: MappingOptions,
        filename: NamespaceNode,
    ) -> Result<MappedVmo, Errno> {
        if flags.contains(zx::VmarFlags::PERM_READ) && !self.can_read() {
            return error!(EACCES);
        }
        if flags.contains(zx::VmarFlags::PERM_WRITE)
            && !self.can_write()
            && options.contains(MappingOptions::SHARED)
        {
            return error!(EACCES);
        }
        // TODO: Check for PERM_EXECUTE by checking whether the filesystem is mounted as noexec.
        self.ops().mmap(self, current_task, addr, vmo_offset, length, flags, options, filename)
    }

    pub fn readdir(
        &self,
        current_task: &CurrentTask,
        sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        match self.ops().readdir(self, current_task, sink) {
            // The ENOSPC we catch here is generated by DirentSink::add. We
            // return the error to the caller only if we didn't have space for
            // the first directory entry.
            //
            // We use ENOSPC rather than EINVAL to signal this condition
            // because EINVAL is a very generic error. We only want to perform
            // this transformation in exactly the case where there was not
            // sufficient space in the DirentSink.
            Err(errno) if errno == ENOSPC && sink.actual() > 0 => Ok(()),
            Err(errno) if errno == ENOSPC => error!(EINVAL),
            result => result,
        }
    }

    pub fn ioctl(
        &self,
        current_task: &CurrentTask,
        request: u32,
        user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.ops().ioctl(self, current_task, request, user_addr)
    }

    pub fn fcntl(
        &self,
        current_task: &CurrentTask,
        cmd: u32,
        arg: u64,
    ) -> Result<SyscallResult, Errno> {
        self.ops().fcntl(self, current_task, cmd, arg)
    }

    pub fn update_file_flags(&self, value: OpenFlags, mask: OpenFlags) {
        let mask_bits = mask.bits();
        let mut flags = self.flags.lock();
        let bits = (flags.bits() & !mask_bits) | (value.bits() & mask_bits);
        *flags = OpenFlags::from_bits_truncate(bits);
    }

    pub fn flags(&self) -> OpenFlags {
        *self.flags.lock()
    }

    /// Get the async owner of this file.
    ///
    /// See fcntl(F_GETOWN)
    pub fn get_async_owner(&self) -> pid_t {
        *self.async_owner.lock()
    }

    /// Set the async owner of this file.
    ///
    /// See fcntl(F_SETOWN)
    pub fn set_async_owner(&self, owner: pid_t) {
        *self.async_owner.lock() = owner;
    }

    /// Wait on the specified events and call the EventHandler when ready
    pub fn wait_async(
        &self,
        current_task: &CurrentTask,
        waiter: &Waiter,
        events: FdEvents,
        handler: EventHandler,
        options: WaitAsyncOptions,
    ) -> WaitKey {
        self.ops().wait_async(self, current_task, waiter, events, handler, options)
    }

    // Cancel a wait set up with wait_async
    pub fn cancel_wait(&self, current_task: &CurrentTask, waiter: &Waiter, key: WaitKey) {
        self.ops().cancel_wait(current_task, waiter, key);
    }

    // Return the events currently active
    pub fn query_events(&self, current_task: &CurrentTask) -> FdEvents {
        self.ops().query_events(current_task)
    }

    //
    /// Updates the file's seek offset without an upper bound on the resulting offset.
    ///
    /// This is useful for files without a defined size.
    ///
    /// Errors if `whence` is invalid, or the calculated offset is invalid.
    ///
    /// - `offset`: The target offset from `whence`.
    /// - `whence`: The location from which to compute the updated offset.
    pub fn unbounded_seek(&self, offset: off_t, whence: SeekOrigin) -> Result<off_t, Errno> {
        let mut current_offset = self.offset.lock();
        let new_offset = match whence {
            SeekOrigin::Set => Some(offset),
            SeekOrigin::Cur => (*current_offset).checked_add(offset),
            SeekOrigin::End => Some(MAX_LFS_FILESIZE as i64),
        }
        .ok_or_else(|| errno!(EINVAL))?;

        if new_offset < 0 {
            return error!(EINVAL);
        }

        *current_offset = new_offset;
        Ok(*current_offset)
    }

    pub fn record_lock(
        &self,
        current_task: &CurrentTask,
        cmd: u32,
        flock: uapi::flock,
    ) -> Result<Option<uapi::flock>, Errno> {
        self.name.entry.node.record_lock(current_task, self, cmd, flock)
    }
}

impl Drop for FileObject {
    fn drop(&mut self) {
        self.ops().close(self);
        self.name.entry.node.on_file_closed();
    }
}

impl fmt::Debug for FileObject {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("FileObject")
            .field("name", &self.name)
            .field("offset", &self.offset)
            .finish()
    }
}
