// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_io as fio;
use fuchsia_zircon as zx;
use std::ffi::CString;
use std::sync::Arc;
use zerocopy::AsBytes;

use crate::fs::fuchsia::RemoteFs;
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::mm::{
    syscalls::{sys_mmap, sys_mremap},
    MemoryAccessor, MemoryManager, PAGE_SIZE,
};
use crate::syscalls::SyscallResult;
use crate::task::*;
use crate::types::*;

/// Create a FileSystemHandle for use in testing.
///
/// Open "/pkg" and returns an FsContext rooted in that directory.
fn create_pkgfs(kernel: &Kernel) -> FileSystemHandle {
    let rights = fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE;
    let (server, client) = zx::Channel::create().expect("failed to create channel");
    fdio::open("/pkg", rights, server).expect("failed to open /pkg");
    RemoteFs::new_fs(kernel, client, rights).unwrap()
}

/// Creates a `Kernel` and `Task` with the package file system for testing purposes.
///
/// The `Task` is backed by a real process, and can be used to test syscalls.
pub fn create_kernel_and_task_with_pkgfs() -> (Arc<Kernel>, CurrentTask) {
    create_kernel_and_task_with_fs(create_pkgfs)
}

pub fn create_kernel_and_task() -> (Arc<Kernel>, CurrentTask) {
    create_kernel_and_task_with_fs(TmpFs::new_fs)
}
/// Creates a `Kernel` and `Task` for testing purposes.
///
/// The `Task` is backed by a real process, and can be used to test syscalls.
fn create_kernel_and_task_with_fs(
    create_fs: impl FnOnce(&Kernel) -> FileSystemHandle,
) -> (Arc<Kernel>, CurrentTask) {
    let kernel = Arc::new(
        Kernel::new(&CString::new("test-kernel").unwrap(), &Vec::new())
            .expect("failed to create kernel"),
    );

    let task = Task::create_process_without_parent(
        &kernel,
        CString::new("test-task").unwrap(),
        Some(FsContext::new(create_fs(&kernel))),
    )
    .expect("failed to create first task");

    // Take the lock on thread group and task in the correct order to ensure any wrong ordering
    // will trigger the tracing-mutex at the right call site.
    {
        let _l1 = task.thread_group.read();
        let _l2 = task.read();
    }

    (kernel, task)
}

/// Creates a new `Task` in the provided kernel.
///
/// The `Task` is backed by a real process, and can be used to test syscalls.
pub fn create_task(kernel: &Arc<Kernel>, task_name: &str) -> CurrentTask {
    let task = Task::create_process_without_parent(
        kernel,
        CString::new(task_name).unwrap(),
        Some(FsContext::new(create_pkgfs(kernel))),
    )
    .expect("failed to create second task");

    // Take the lock on thread group and task in the correct order to ensure any wrong ordering
    // will trigger the tracing-mutex at the right call site.
    {
        let _l1 = task.thread_group.read();
        let _l2 = task.read();
    }

    task
}

/// Maps `length` at `address` with `PROT_READ | PROT_WRITE`, `MAP_ANONYMOUS | MAP_PRIVATE`.
///
/// Returns the address returned by `sys_mmap`.
pub fn map_memory(current_task: &CurrentTask, address: UserAddress, length: u64) -> UserAddress {
    sys_mmap(
        current_task,
        address,
        length as usize,
        PROT_READ | PROT_WRITE,
        MAP_ANONYMOUS | MAP_PRIVATE,
        FdNumber::from_raw(-1),
        0,
    )
    .expect("Could not map memory")
}

/// Convenience wrapper around [`sys_mremap`] which extracts the returned [`UserAddress`] from
/// the generic [`SyscallResult`].
pub fn remap_memory(
    current_task: &CurrentTask,
    old_addr: UserAddress,
    old_length: u64,
    new_length: u64,
    flags: u32,
    new_addr: UserAddress,
) -> Result<UserAddress, Errno> {
    sys_mremap(current_task, old_addr, old_length as usize, new_length as usize, flags, new_addr)
}

/// Fills one page in the `current_task`'s address space starting at `addr` with the ASCII character
/// `data`. Panics if the write failed.
///
/// This method uses the `#[track_caller]` attribute, which will display the caller's file and line
/// number in the event of a panic. This makes it easier to find test regressions.
#[track_caller]
pub fn fill_page(current_task: &CurrentTask, addr: UserAddress, data: char) {
    let data = [data as u8].repeat(*PAGE_SIZE as usize);
    if let Err(err) = current_task.mm.write_memory(addr, &data) {
        panic!("write page: failed to fill page @ {:?} with {:?}: {:?}", addr, data, err);
    }
}

/// Checks that the page in `current_task`'s address space starting at `addr` is readable.
/// Panics if the read failed, or the page was not filled with the ASCII character `data`.
///
/// This method uses the `#[track_caller]` attribute, which will display the caller's file and line
/// number in the event of a panic. This makes it easier to find test regressions.
#[track_caller]
pub fn check_page_eq(current_task: &CurrentTask, addr: UserAddress, data: char) {
    let mut buf = vec![0; *PAGE_SIZE as usize];
    if let Err(err) = current_task.mm.read_memory(addr, &mut buf) {
        panic!("read page: failed to read page @ {:?}: {:?}", addr, err);
    }
    assert!(
        buf.into_iter().all(|c| c == data as u8),
        "unexpected payload: page @ {:?} should be filled with {:?}",
        addr,
        data
    );
}

/// Checks that the page in `current_task`'s address space starting at `addr` is readable.
/// Panics if the read failed, or the page *was* filled with the ASCII character `data`.
///
/// This method uses the `#[track_caller]` attribute, which will display the caller's file and line
/// number in the event of a panic. This makes it easier to find test regressions.
#[track_caller]
pub fn check_page_ne(current_task: &CurrentTask, addr: UserAddress, data: char) {
    let mut buf = vec![0; *PAGE_SIZE as usize];
    if let Err(err) = current_task.mm.read_memory(addr, &mut buf) {
        panic!("read page: failed to read page @ {:?}: {:?}", addr, err);
    }
    assert!(
        !buf.into_iter().all(|c| c == data as u8),
        "unexpected payload: page @ {:?} should not be filled with {:?}",
        addr,
        data
    );
}

/// Checks that the page in `current_task`'s address space starting at `addr` is unmapped.
/// Panics if the read succeeds, or if an error other than `EFAULT` occurs.
///
/// This method uses the `#[track_caller]` attribute, which will display the caller's file and line
/// number in the event of a panic. This makes it easier to find test regressions.
#[track_caller]
pub fn check_unmapped(current_task: &CurrentTask, addr: UserAddress) {
    let mut buf = vec![0; *PAGE_SIZE as usize];
    match current_task.mm.read_memory(addr, &mut buf) {
        Ok(()) => panic!("read page: page @ {:?} should be unmapped", addr),
        Err(err) if err == crate::types::errno::EFAULT => {}
        Err(err) => {
            panic!("read page: expected EFAULT reading page @ {:?} but got {:?} instead", addr, err)
        }
    }
}

/// An FsNodeOps implementation that panics if you try to open it. Useful as a stand-in for testing
/// APIs that require a FsNodeOps implementation but don't actually use it.
pub struct PanickingFsNode;

impl FsNodeOps for PanickingFsNode {
    fn create_file_ops(
        &self,
        _node: &FsNode,
        _flags: OpenFlags,
    ) -> Result<Box<dyn FileOps>, Errno> {
        panic!("should not be called")
    }
}

/// An implementation of [`FileOps`] that panics on any read, write, or ioctl operation.
pub struct PanickingFile;

impl PanickingFile {
    /// Creates a [`FileObject`] whose implementation panics on reads, writes, and ioctls.
    pub fn new_file(current_task: &CurrentTask) -> FileHandle {
        Anon::new_file(current_task, Box::new(PanickingFile), OpenFlags::RDWR)
    }
}

impl FileOps for PanickingFile {
    fileops_impl_nonseekable!();
    fileops_impl_nonblocking!();

    fn write(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        panic!("write called on TestFile")
    }

    fn read(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        panic!("read called on TestFile")
    }

    fn ioctl(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _request: u32,
        _user_addr: UserAddress,
    ) -> Result<SyscallResult, Errno> {
        panic!("ioctl called on TestFile")
    }
}

/// Helper to write out data to a task's memory sequentially.
pub struct UserMemoryWriter<'a> {
    // The task's memory manager.
    mm: &'a MemoryManager,
    // The address to which to write the next bit of data.
    current_addr: UserAddress,
}

impl<'a> UserMemoryWriter<'a> {
    /// Constructs a new `UserMemoryWriter` to write to `task`'s memory at `addr`.
    pub fn new(task: &'a Task, addr: UserAddress) -> Self {
        Self { mm: &task.mm, current_addr: addr }
    }

    /// Writes all of `data` to the current address in the task's address space, incrementing the
    /// current address by the size of `data`. Returns the address at which the data starts.
    /// Panics on failure.
    pub fn write(&mut self, data: &[u8]) -> UserAddress {
        let bytes_written = self.mm.write_memory(self.current_addr, data).unwrap();
        assert_eq!(bytes_written, data.len());
        let start_addr = self.current_addr;
        self.current_addr += bytes_written;
        start_addr
    }

    /// Writes `object` to the current address in the task's address space, incrementing the
    /// current address by the size of `object`. Returns the address at which the data starts.
    /// Panics on failure.
    pub fn write_object<T: AsBytes>(&mut self, object: &T) -> UserAddress {
        self.write(object.as_bytes())
    }

    /// Returns the current address at which data will be next written.
    pub fn current_address(&self) -> UserAddress {
        self.current_addr
    }
}
