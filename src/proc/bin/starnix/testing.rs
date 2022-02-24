// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::Proxy;
use fidl_fuchsia_io as fio;
use io_util::directory;
use std::ffi::CString;
use std::sync::Arc;

use crate::fs::fuchsia::RemoteFs;
use crate::fs::tmpfs::TmpFs;
use crate::fs::*;
use crate::mm::{
    syscalls::{sys_mmap, sys_mremap},
    PAGE_SIZE,
};
use crate::task::*;
use crate::types::*;

/// Create an FsContext for use in testing.
///
/// Open "/pkg" and returns an FsContext rooted in that directory.
fn create_pkgfs() -> Arc<FsContext> {
    let root =
        directory::open_in_namespace("/pkg", fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE)
            .expect("failed to open /pkg");
    return FsContext::new(
        RemoteFs::new(
            root.into_channel().unwrap().into_zx_channel(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .unwrap(),
    );
}

/// Creates a `Kernel` and `Task` with the package file system for testing purposes.
///
/// The `Task` is backed by a real process, and can be used to test syscalls.
pub fn create_kernel_and_task_with_pkgfs() -> (Arc<Kernel>, CurrentTask) {
    create_kernel_and_task_with_fs(create_pkgfs())
}

pub fn create_kernel_and_task() -> (Arc<Kernel>, CurrentTask) {
    create_kernel_and_task_with_fs(FsContext::new(TmpFs::new()))
}
/// Creates a `Kernel` and `Task` for testing purposes.
///
/// The `Task` is backed by a real process, and can be used to test syscalls.
pub fn create_kernel_and_task_with_fs(fs: Arc<FsContext>) -> (Arc<Kernel>, CurrentTask) {
    let kernel = Arc::new(
        Kernel::new(&CString::new("test-kernel").unwrap()).expect("failed to create kernel"),
    );

    let task = Task::create_process_without_parent(&kernel, CString::new("test-task").unwrap(), fs)
        .expect("failed to create first task");
    *task.exit_code.lock() = Some(0);

    (kernel, task)
}

/// Creates a new `Task` in the provided kernel.
///
/// The `Task` is backed by a real process, and can be used to test syscalls.
pub fn create_task(kernel: &Arc<Kernel>, task_name: &str) -> CurrentTask {
    let task = Task::create_process_without_parent(
        kernel,
        CString::new(task_name).unwrap(),
        create_pkgfs(),
    )
    .expect("failed to create second task");
    *task.exit_code.lock() = Some(0);
    task
}

/// Maps `length` at `address` with `PROT_READ | PROT_WRITE`, `MAP_ANONYMOUS | MAP_PRIVATE`.
///
/// Returns the address returned by `sys_mmap`.
pub fn map_memory(current_task: &CurrentTask, address: UserAddress, length: u64) -> UserAddress {
    sys_mmap(
        &current_task,
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
    let mut buf = Vec::with_capacity(*PAGE_SIZE as usize);
    buf.resize(*PAGE_SIZE as usize, 0u8);
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
    let mut buf = Vec::with_capacity(*PAGE_SIZE as usize);
    buf.resize(*PAGE_SIZE as usize, 0u8);
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
    let mut buf = Vec::with_capacity(*PAGE_SIZE as usize);
    buf.resize(*PAGE_SIZE as usize, 0u8);
    match current_task.mm.read_memory(addr, &mut buf) {
        Ok(()) => panic!("read page: page @ {:?} should be unmapped", addr),
        Err(err) if err.value() == crate::types::uapi::EFAULT as i32 => {}
        Err(err) => {
            panic!("read page: expected EFAULT reading page @ {:?} but got {:} instead", addr, err)
        }
    }
}
