// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef};
use std::ffi::CStr;
use std::sync::Arc;

use crate::fs::*;
use crate::logging::*;
use crate::mm::*;
use crate::syscalls::*;
use crate::types::*;
use crate::vmex_resource::VMEX_RESOURCE;
use crate::{errno, error, not_implemented, strace};

fn mmap_prot_to_vm_opt(prot: u32) -> zx::VmarFlags {
    let mut flags = zx::VmarFlags::empty();
    if prot & PROT_READ != 0 {
        flags |= zx::VmarFlags::PERM_READ;
    }
    if prot & PROT_WRITE != 0 {
        flags |= zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE;
    }
    if prot & PROT_EXEC != 0 {
        flags |= zx::VmarFlags::PERM_EXECUTE;
    }
    flags
}

pub fn sys_mmap(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
    prot: u32,
    flags: u32,
    fd: FdNumber,
    offset: u64,
) -> Result<SyscallResult, Errno> {
    // These are the flags that are currently supported.
    if prot & !(PROT_READ | PROT_WRITE | PROT_EXEC) != 0 {
        not_implemented!("mmap: prot: 0x{:x}", prot);
        return error!(EINVAL);
    }
    if flags & MAP_32BIT != 0 {
        not_implemented!("mmap flag MAP_32BIT not implemented.");
        return error!(ENOSYS);
    }
    if flags
        & !(MAP_PRIVATE
            | MAP_SHARED
            | MAP_ANONYMOUS
            | MAP_FIXED
            | MAP_FIXED_NOREPLACE
            | MAP_POPULATE
            | MAP_NORESERVE
            | MAP_STACK)
        != 0
    {
        not_implemented!("mmap: flags: 0x{:x}", flags);
        return error!(EINVAL);
    }

    if flags & (MAP_PRIVATE | MAP_SHARED) == 0
        || flags & (MAP_PRIVATE | MAP_SHARED) == MAP_PRIVATE | MAP_SHARED
    {
        return error!(EINVAL);
    }
    if length == 0 {
        return error!(EINVAL);
    }
    if offset % *PAGE_SIZE != 0 {
        return error!(EINVAL);
    }

    // TODO(tbodt): should we consider MAP_NORESERVE?

    if flags & MAP_ANONYMOUS != 0 && fd.raw() != -1 {
        return error!(EINVAL);
    }

    let mut zx_flags = mmap_prot_to_vm_opt(prot) | zx::VmarFlags::ALLOW_FAULTS;
    if addr.ptr() != 0 {
        zx_flags |= zx::VmarFlags::SPECIFIC;
    }
    if flags & MAP_FIXED != 0 && flags & MAP_FIXED_NOREPLACE == 0 {
        // SAFETY: We are operating on another process, so it's safe to use SPECIFIC_OVERWRITE
        zx_flags |= unsafe {
            zx::VmarFlags::from_bits_unchecked(zx::VmarFlagsExtended::SPECIFIC_OVERWRITE.bits())
        };
    }

    let mut filename = None;
    let vmo = if flags & MAP_ANONYMOUS != 0 {
        // mremap can grow memory regions, so make sure the VMO is resizable.
        let mut vmo = zx::Vmo::create_with_opts(zx::VmoOptions::RESIZABLE, length as u64).map_err(
            |s| match s {
                zx::Status::NO_MEMORY => errno!(ENOMEM),
                zx::Status::OUT_OF_RANGE => errno!(ENOMEM),
                _ => impossible_error(s),
            },
        )?;
        vmo.set_name(CStr::from_bytes_with_nul(b"starnix-anon\0").unwrap())
            .map_err(impossible_error)?;
        if zx_flags.contains(zx::VmarFlags::PERM_EXECUTE) {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        vmo
    } else {
        // TODO(tbodt): maximize protection flags so that mprotect works
        let file = current_task.files.get(fd)?;
        filename = Some(file.name.clone());
        let zx_prot = mmap_prot_to_vm_opt(prot);
        if flags & MAP_PRIVATE != 0 {
            // TODO(tbodt): Use VMO_FLAG_PRIVATE to have the filesystem server do the clone for us.
            let vmo = file.get_vmo(&current_task, zx_prot - zx::VmarFlags::PERM_WRITE)?;
            let mut clone_flags = zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE;
            if !zx_prot.contains(zx::VmarFlags::PERM_WRITE) {
                clone_flags |= zx::VmoChildOptions::NO_WRITE;
            }
            vmo.create_child(clone_flags, 0, vmo.get_size().map_err(impossible_error)?)
                .map_err(impossible_error)?
        } else {
            file.get_vmo(&current_task, zx_prot)?
        }
    };

    let vmo = Arc::new(vmo);
    let vmo_offset = if flags & MAP_ANONYMOUS != 0 { 0 } else { offset };

    let mut options = MappingOptions::empty();
    if flags & MAP_SHARED != 0 {
        options |= MappingOptions::SHARED;
    }
    if flags & MAP_ANONYMOUS != 0 {
        options |= MappingOptions::ANONYMOUS;
    }

    let try_map = |addr, flags| {
        current_task.mm.map(
            addr,
            Arc::clone(&vmo),
            vmo_offset,
            length,
            flags,
            options,
            filename.clone(),
        )
    };
    let addr = match try_map(addr, zx_flags) {
        Err(errno) if zx_flags.contains(zx::VmarFlags::SPECIFIC) => {
            if flags & MAP_FIXED_NOREPLACE != 0 {
                if errno == ENOMEM {
                    return error!(EEXIST);
                }
                return Err(errno);
            }
            if flags & MAP_FIXED != 0 {
                return Err(errno);
            }
            try_map(UserAddress::default(), zx_flags - zx::VmarFlags::SPECIFIC)
        }
        result => result,
    }?;

    if flags & MAP_POPULATE != 0 {
        let _result = vmo.op_range(zx::VmoOp::COMMIT, vmo_offset, length as u64);
        // "The mmap() call doesn't fail if the mapping cannot be populated."
    }

    Ok(addr.into())
}

pub fn sys_mprotect(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
    prot: u32,
) -> Result<SyscallResult, Errno> {
    current_task.mm.protect(addr, length, mmap_prot_to_vm_opt(prot))?;
    Ok(SUCCESS)
}

pub fn sys_munmap(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
) -> Result<SyscallResult, Errno> {
    current_task.mm.unmap(addr, length)?;
    Ok(SUCCESS)
}

pub fn sys_msync(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
    _flags: u32,
) -> Result<SyscallResult, Errno> {
    not_implemented!("msync not implemented");
    // Perform some basic validation of the address range given to satisfy gvisor tests that
    // use msync as a way to probe whether a page is mapped or not.
    current_task.mm.ensure_mapped(addr, length)?;
    Ok(SUCCESS)
}

pub fn sys_madvise(
    _current_task: &CurrentTask,
    _addr: UserAddress,
    _length: usize,
    _advice: u32,
) -> Result<SyscallResult, Errno> {
    not_implemented!("madvise not implemented");
    Ok(SUCCESS)
}

pub fn sys_brk(current_task: &CurrentTask, addr: UserAddress) -> Result<SyscallResult, Errno> {
    Ok(current_task.mm.set_brk(addr)?.into())
}

pub fn sys_process_vm_readv(
    current_task: &CurrentTask,
    pid: pid_t,
    local_iov_addr: UserAddress,
    local_iov_count: i32,
    remote_iov_addr: UserAddress,
    remote_iov_count: i32,
    _flags: usize,
) -> Result<SyscallResult, Errno> {
    let task = current_task.get_task(pid).ok_or(errno!(ESRCH))?;
    // When this check is loosened to allow reading memory from other processes, the check should
    // be like checking if the current process is allowed to debug the other process.
    if !Arc::ptr_eq(&task, &current_task.task_arc_clone()) {
        return error!(EPERM);
    }
    let local_iov = task.mm.read_iovec(local_iov_addr, local_iov_count)?;
    let remote_iov = task.mm.read_iovec(remote_iov_addr, remote_iov_count)?;
    strace!(
        current_task,
        "process_vm_readv(pid={}, local_iov={:?}, remote_iov={:?})",
        pid,
        local_iov,
        remote_iov
    );
    // TODO(tbodt): According to the man page, this syscall was added to Linux specifically to
    // avoid doing two copies like other IPC mechanisms require. We should avoid this too at some
    // point.
    let len = std::cmp::min(
        UserBuffer::get_total_length(&local_iov),
        UserBuffer::get_total_length(&remote_iov),
    );
    let mut buf = vec![0u8; len];
    let len = task.mm.read_all(&remote_iov, &mut buf)?;
    let len = task.mm.write_all(&local_iov, &buf[..len])?;
    Ok(len.into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_mmap_with_colliding_hint() {
        let (_kernel, current_task) = create_kernel_and_task();
        let page_size = *PAGE_SIZE;

        let mapped_address = map_memory(&current_task, UserAddress::default(), page_size);
        match sys_mmap(
            &current_task,
            mapped_address,
            page_size as usize,
            PROT_READ,
            MAP_PRIVATE | MAP_ANONYMOUS,
            FdNumber::from_raw(-1),
            0,
        ) {
            Ok(SyscallResult::Success(address)) => {
                assert_ne!(UserAddress::from(address), mapped_address);
            }
            result => {
                assert!(false, "mmap with colliding hint failed: {:?}", result);
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mmap_with_fixed_collision() {
        let (_kernel, current_task) = create_kernel_and_task();
        let page_size = *PAGE_SIZE;

        let mapped_address = map_memory(&current_task, UserAddress::default(), page_size);
        match sys_mmap(
            &current_task,
            mapped_address,
            page_size as usize,
            PROT_READ,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
            FdNumber::from_raw(-1),
            0,
        ) {
            Ok(SyscallResult::Success(address)) => {
                assert_eq!(UserAddress::from(address), mapped_address);
            }
            result => {
                assert!(false, "mmap with fixed collision failed: {:?}", result);
            }
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mmap_with_fixed_noreplace_collision() {
        let (_kernel, current_task) = create_kernel_and_task();
        let page_size = *PAGE_SIZE;

        let mapped_address = map_memory(&current_task, UserAddress::default(), page_size);
        match sys_mmap(
            &current_task,
            mapped_address,
            page_size as usize,
            PROT_READ,
            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
            FdNumber::from_raw(-1),
            0,
        ) {
            Err(errno) => {
                assert_eq!(errno, EEXIST);
            }
            result => {
                assert!(false, "mmap with fixed_noreplace collision failed: {:?}", result);
            }
        }
    }

    /// It is ok to call munmap with an address that is a multiple of the page size, and
    /// a non-zero length.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));

        // Verify that the memory is no longer readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
    }

    /// It is ok to call munmap on an unmapped range.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_mapped() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
        assert_eq!(sys_munmap(&current_task, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
    }

    /// It is an error to call munmap with a length of 0.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_0_length() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, 0), error!(EINVAL));
    }

    /// It is an error to call munmap with an address that is not a multiple of the page size.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_aligned() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(
            sys_munmap(&current_task, mapped_address + 1u64, *PAGE_SIZE as usize),
            error!(EINVAL)
        );

        // Verify that the memory is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), Ok(()));
    }

    /// The entire page should be unmapped, not just the range [address, address + length).
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_unmap_partial() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(
            sys_munmap(&current_task, mapped_address, (*PAGE_SIZE as usize) / 2),
            Ok(SUCCESS)
        );

        // Verify that memory can't be read in either half of the page.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + (*PAGE_SIZE - 2), &mut data),
            error!(EFAULT)
        );
    }

    /// All pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_multiple_pages() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(
            sys_munmap(&current_task, mapped_address, (*PAGE_SIZE as usize) + 1),
            Ok(SUCCESS)
        );

        // Verify that neither page is readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data),
            error!(EFAULT)
        );
    }

    /// Only the pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_one_of_many_pages() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(
            sys_munmap(&current_task, mapped_address, (*PAGE_SIZE as usize) - 1),
            Ok(SUCCESS)
        );

        // Verify that the second page is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data),
            Ok(())
        );
    }

    /// Unmap the middle page of a mapping.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_middle_page() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        assert_eq!(
            sys_munmap(&current_task, mapped_address + *PAGE_SIZE, *PAGE_SIZE as usize),
            Ok(SUCCESS)
        );

        // Verify that the first and third pages are still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), Ok(()));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + *PAGE_SIZE, &mut data),
            error!(EFAULT)
        );
        assert_eq!(
            current_task.mm.read_memory(mapped_address + (*PAGE_SIZE * 2), &mut data),
            Ok(())
        );
    }

    /// Unmap a range of pages that includes disjoint mappings.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_many_mappings() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_addresses: Vec<_> = std::iter::repeat_with(|| {
            map_memory(&current_task, UserAddress::default(), *PAGE_SIZE)
        })
        .take(3)
        .collect();
        let min_address = *mapped_addresses.iter().min().unwrap();
        let max_address = *mapped_addresses.iter().max().unwrap();
        let unmap_length = (max_address - min_address) + *PAGE_SIZE as usize;

        assert_eq!(sys_munmap(&current_task, min_address, unmap_length), Ok(SUCCESS));

        // Verify that none of the mapped pages are readable.
        let mut data: [u8; 5] = [0; 5];
        for mapped_address in mapped_addresses {
            assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_msync_validates_address_range() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 3 pages and test that ranges covering these pages return no error.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 3, 0), Ok(SUCCESS));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 2, 0), Ok(SUCCESS));
        assert_eq!(
            sys_msync(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize * 2, 0),
            Ok(SUCCESS)
        );

        // Unmap the middle page and test that ranges covering that page return ENOMEM.
        sys_munmap(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize).expect("unmap middle");
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize, 0), Ok(SUCCESS));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 3, 0), error!(ENOMEM));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 2, 0), error!(ENOMEM));
        assert_eq!(
            sys_msync(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize * 2, 0),
            error!(ENOMEM)
        );
        assert_eq!(
            sys_msync(&current_task, addr + *PAGE_SIZE * 2, *PAGE_SIZE as usize, 0),
            Ok(SUCCESS)
        );

        // Map the middle page back and test that ranges covering the three pages
        // (spanning multiple ranges) return no error.
        assert_eq!(map_memory(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE), addr + *PAGE_SIZE);
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 3, 0), Ok(SUCCESS));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 2, 0), Ok(SUCCESS));
        assert_eq!(
            sys_msync(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize * 2, 0),
            Ok(SUCCESS)
        );
    }
}
