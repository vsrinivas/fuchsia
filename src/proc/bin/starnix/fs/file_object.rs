// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon as zx;
use parking_lot::Mutex;
use std::fmt;
use std::sync::Arc;

use crate::fs::*;
use crate::not_implemented;
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

pub const MAX_LFS_FILESIZE: usize = 0x7fffffffffffffff;

pub enum SeekOrigin {
    SET,
    CUR,
    END,
}

impl SeekOrigin {
    pub fn from_raw(whence: u32) -> Result<SeekOrigin, Errno> {
        match whence {
            SEEK_SET => Ok(SeekOrigin::SET),
            SEEK_CUR => Ok(SeekOrigin::CUR),
            SEEK_END => Ok(SeekOrigin::END),
            _ => Err(EINVAL),
        }
    }
}

/// Corresponds to struct file_operations in Linux, plus any filesystem-specific data.
pub trait FileOps: Send + Sync {
    /// Called when the FileObject is closed.
    fn close(&self, _file: &FileObject) {}

    /// Read from the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable.
    fn read(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno>;
    /// Read from the file at an offset. If your file is seekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn read_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno>;
    /// Write to the file without an offset. If your file is seekable, consider implementing this
    /// with fd_impl_seekable!.
    fn write(&self, file: &FileObject, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno>;
    /// Write to the file at a offset. If your file is nonseekable, consider implementing this with
    /// fd_impl_nonseekable!.
    fn write_at(
        &self,
        file: &FileObject,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno>;

    /// Adjust the seek offset if the file is seekable.
    fn seek(
        &self,
        file: &FileObject,
        task: &Task,
        offset: off_t,
        whence: SeekOrigin,
    ) -> Result<off_t, Errno>;

    /// Responds to an mmap call by returning a VMO. At least the requested protection flags must
    /// be set on the VMO. Reading or writing the VMO must read or write the file. If this is not
    /// possible given the requested protection, an error must be returned.
    fn get_vmo(
        &self,
        _file: &FileObject,
        _task: &Task,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        Err(ENODEV)
    }

    fn readdir(
        &self,
        _file: &FileObject,
        _task: &Task,
        _sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        Err(ENOTDIR)
    }

    /// Establish a one-shot, asynchronous wait for the given FdEvents for the given file and task.
    fn wait_async(&self, file: &FileObject, waiter: &Arc<Waiter>, events: FdEvents) {
        file.node().observers.wait_async(waiter, events)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _task: &Task,
        request: u32,
        _in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        default_ioctl(request)
    }

    fn fcntl(
        &self,
        _file: &FileObject,
        _task: &Task,
        _cmd: u32,
        _arg: u64,
    ) -> Result<SyscallResult, Errno> {
        Err(EINVAL)
    }
}

/// Implements FileDesc methods in a way that makes sense for nonseekable files. You must implement
/// read and write.
#[macro_export]
macro_rules! fd_impl_nonseekable {
    () => {
        fn read_at(
            &self,
            _file: &FileObject,
            _task: &Task,
            _offset: usize,
            _data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
        fn write_at(
            &self,
            _file: &FileObject,
            _task: &Task,
            _offset: usize,
            _data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            Err(ESPIPE)
        }
        fn seek(
            &self,
            _file: &FileObject,
            _task: &Task,
            _offset: off_t,
            _whence: SeekOrigin,
        ) -> Result<off_t, Errno> {
            Err(ESPIPE)
        }
    };
}

/// Implements FileDesc methods in a way that makes sense for seekable files. You must implement
/// read_at and write_at.
#[macro_export]
macro_rules! fd_impl_seekable {
    () => {
        fn read(
            &self,
            file: &FileObject,
            task: &Task,
            data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            let mut offset = file.offset.lock();
            let size = self.read_at(file, task, *offset as usize, data)?;
            *offset += size as off_t;
            Ok(size)
        }
        fn write(
            &self,
            file: &FileObject,
            task: &Task,
            data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            let mut offset = file.offset.lock();
            if file.flags().contains(OpenFlags::APPEND) {
                *offset = file.node().info().size as off_t;
            }
            let size = self.write_at(file, task, *offset as usize, data)?;
            *offset += size as off_t;
            Ok(size)
        }
        fn seek(
            &self,
            file: &FileObject,
            _task: &Task,
            offset: off_t,
            whence: SeekOrigin,
        ) -> Result<off_t, Errno> {
            let mut current_offset = file.offset.lock();
            let new_offset = match whence {
                SeekOrigin::SET => Some(offset),
                SeekOrigin::CUR => (*current_offset).checked_add(offset),
                SeekOrigin::END => {
                    let stat = file.node().stat()?;
                    offset.checked_add(stat.st_size as off_t)
                }
            }
            .ok_or(EINVAL)?;

            if new_offset < 0 {
                return Err(EINVAL);
            }

            *current_offset = new_offset;
            Ok(*current_offset)
        }
    };
}

#[macro_export]
macro_rules! fd_impl_directory {
    () => {
        fn read(
            &self,
            _file: &FileObject,
            _task: &Task,
            _data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            Err(EISDIR)
        }

        fn read_at(
            &self,
            _file: &FileObject,
            _task: &Task,
            _offset: usize,
            _data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            Err(EISDIR)
        }

        fn write(
            &self,
            _file: &FileObject,
            _task: &Task,
            _data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            Err(EISDIR)
        }

        fn write_at(
            &self,
            _file: &FileObject,
            _task: &Task,
            _offset: usize,
            _data: &[UserBuffer],
        ) -> Result<usize, Errno> {
            Err(EISDIR)
        }
    };
}

pub fn default_ioctl(request: u32) -> Result<SyscallResult, Errno> {
    not_implemented!("ioctl: request=0x{:x}", request);
    Err(ENOTTY)
}

pub struct OPathOps {}

impl OPathOps {
    pub fn new() -> OPathOps {
        OPathOps {}
    }
}

impl FileOps for OPathOps {
    fn read(&self, _file: &FileObject, _task: &Task, _data: &[UserBuffer]) -> Result<usize, Errno> {
        Err(EBADF)
    }
    fn read_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(EBADF)
    }
    fn write(
        &self,
        _file: &FileObject,
        _task: &Task,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(EBADF)
    }
    fn write_at(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(EBADF)
    }
    fn seek(
        &self,
        _file: &FileObject,
        _task: &Task,
        _offset: off_t,
        _whence: SeekOrigin,
    ) -> Result<off_t, Errno> {
        Err(EBADF)
    }
    fn get_vmo(
        &self,
        _file: &FileObject,
        _task: &Task,
        _prot: zx::VmarFlags,
    ) -> Result<zx::Vmo, Errno> {
        Err(EBADF)
    }
    fn readdir(
        &self,
        _file: &FileObject,
        _task: &Task,
        _sink: &mut dyn DirentSink,
    ) -> Result<(), Errno> {
        Err(EBADF)
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _task: &Task,
        _request: u32,
        _in_addr: UserAddress,
        _out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        Err(EBADF)
    }

    fn fcntl(
        &self,
        _file: &FileObject,
        _task: &Task,
        _cmd: u32,
        _arg: u64,
    ) -> Result<SyscallResult, Errno> {
        // Note: this can be a valid operation for files opened with O_PATH.
        Err(EINVAL)
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

    _fs: FileSystemHandle,

    pub offset: Mutex<off_t>,

    flags: Mutex<OpenFlags>,

    async_owner: Mutex<pid_t>,
}

pub type FileHandle = Arc<FileObject>;

impl FileObject {
    /// Create a FileObject that is not mounted in a namespace.
    ///
    /// The returned FileObject does not have a name.
    pub fn new_anonymous(
        ops: Box<dyn FileOps>,
        node: FsNodeHandle,
        flags: OpenFlags,
    ) -> FileHandle {
        Self::new(ops, NamespaceNode::new_anonymous(node), flags)
    }

    /// Create a FileObject with an associated NamespaceNode.
    ///
    /// This function is not typically called directly. Instead, consider
    /// calling NamespaceNode::open.
    pub fn new(ops: Box<dyn FileOps>, name: NamespaceNode, flags: OpenFlags) -> FileHandle {
        let fs = name.entry.node.fs();
        Arc::new(Self {
            name,
            _fs: fs,
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
        &*self.ops
    }

    pub fn blocking_op<T, Op>(&self, task: &Task, mut op: Op, events: FdEvents) -> Result<T, Errno>
    where
        Op: FnMut() -> Result<T, Errno>,
    {
        loop {
            match op() {
                Err(EAGAIN) if !self.flags().contains(OpenFlags::NONBLOCK) => {
                    self.ops().wait_async(self, &task.waiter, events);
                    task.waiter.wait()?;
                }
                result => return result,
            }
        }
    }

    pub fn read(&self, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        if !self.can_read() {
            return Err(EBADF);
        }
        self.blocking_op(task, || self.ops().read(self, task, data), FdEvents::POLLIN)
    }

    pub fn read_at(&self, task: &Task, offset: usize, data: &[UserBuffer]) -> Result<usize, Errno> {
        if !self.can_read() {
            return Err(EBADF);
        }
        self.blocking_op(task, || self.ops().read_at(self, task, offset, data), FdEvents::POLLIN)
    }

    pub fn write(&self, task: &Task, data: &[UserBuffer]) -> Result<usize, Errno> {
        if !self.can_write() {
            return Err(EBADF);
        }
        self.blocking_op(
            task,
            || {
                if self.flags().contains(OpenFlags::APPEND) {
                    let _guard = self.node().append_lock.write();
                    self.ops().write(self, task, data)
                } else {
                    let _guard = self.node().append_lock.read();
                    self.ops().write(self, task, data)
                }
            },
            FdEvents::POLLOUT,
        )
    }

    pub fn write_at(
        &self,
        task: &Task,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        if !self.can_write() {
            return Err(EBADF);
        }
        self.blocking_op(
            task,
            || {
                let _guard = self.node().append_lock.read();
                self.ops().write_at(self, task, offset, data)
            },
            FdEvents::POLLOUT,
        )
    }

    pub fn seek(&self, task: &Task, offset: off_t, whence: SeekOrigin) -> Result<off_t, Errno> {
        self.ops().seek(self, task, offset, whence)
    }

    pub fn get_vmo(&self, task: &Task, prot: zx::VmarFlags) -> Result<zx::Vmo, Errno> {
        if prot.contains(zx::VmarFlags::PERM_READ) && !self.can_read() {
            return Err(EACCES);
        }
        if prot.contains(zx::VmarFlags::PERM_WRITE) && !self.can_write() {
            return Err(EACCES);
        }
        // TODO: Check for PERM_EXECUTE by checking whether the filesystem is mounted as noexec.
        self.ops().get_vmo(self, task, prot)
    }

    pub fn readdir(&self, task: &Task, sink: &mut dyn DirentSink) -> Result<(), Errno> {
        match self.ops().readdir(self, task, sink) {
            // The ENOSPC we catch here is generated by DirentSink::add. We
            // return the error to the caller only if we didn't have space for
            // the first directory entry.
            //
            // We use ENOSPC rather than EINVAL to signal this condition
            // because EINVAL is a very generic error. We only want to perform
            // this transformation in exactly the case where there was not
            // sufficient space in the DirentSink.
            Err(ENOSPC) if sink.actual() > 0 => Ok(()),
            Err(ENOSPC) => Err(EINVAL),
            result => result,
        }
    }

    pub fn ioctl(
        &self,
        task: &Task,
        request: u32,
        in_addr: UserAddress,
        out_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        self.ops().ioctl(self, task, request, in_addr, out_addr)
    }

    pub fn fcntl(&self, task: &Task, cmd: u32, arg: u64) -> Result<SyscallResult, Errno> {
        self.ops().fcntl(self, task, cmd, arg)
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
}

impl Drop for FileObject {
    fn drop(&mut self) {
        self.ops().close(self);
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
