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
use crate::vmex_resource::VMEX_RESOURCE;

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
) -> Result<UserAddress, Errno> {
    // These are the flags that are currently supported.
    if prot & !(PROT_READ | PROT_WRITE | PROT_EXEC) != 0 {
        not_implemented!(current_task, "mmap: prot: 0x{:x}", prot);
        return error!(EINVAL);
    }
    if flags
        & !(MAP_32BIT
            | MAP_PRIVATE
            | MAP_SHARED
            | MAP_ANONYMOUS
            | MAP_FIXED
            | MAP_FIXED_NOREPLACE
            | MAP_POPULATE
            | MAP_NORESERVE
            | MAP_STACK
            | MAP_DENYWRITE)
        != 0
    {
        not_implemented!(current_task, "mmap: flags: 0x{:x}", flags);
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
    let addr = if flags & MAP_FIXED != 0 || flags & MAP_FIXED_NOREPLACE != 0 {
        DesiredAddress::Fixed(addr)
    } else {
        DesiredAddress::Hint(addr)
    };

    let vmo_offset = if flags & MAP_ANONYMOUS != 0 { 0 } else { offset };

    let mut options = MappingOptions::empty();
    if flags & MAP_SHARED != 0 {
        options |= MappingOptions::SHARED;
    }
    if flags & MAP_ANONYMOUS != 0 {
        options |= MappingOptions::ANONYMOUS;
    }
    if flags & MAP_FIXED == 0 && flags & MAP_32BIT != 0 {
        options |= MappingOptions::LOWER_32BIT;
    }

    let MappedVmo { vmo, user_address } = if flags & MAP_ANONYMOUS != 0 {
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
        // TODO(fxbug.dev/105639): Audit replace_as_executable usage
        vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        let vmo = Arc::new(vmo);
        let user_address =
            current_task.mm.map(addr, vmo.clone(), vmo_offset, length, zx_flags, options, None)?;
        MappedVmo::new(vmo, user_address)
    } else {
        // TODO(tbodt): maximize protection flags so that mprotect works
        let file = current_task.files.get(fd)?;
        file.mmap(current_task, addr, vmo_offset, length, zx_flags, options, file.name.clone())?
    };

    if flags & MAP_POPULATE != 0 {
        let _result = vmo.op_range(zx::VmoOp::COMMIT, vmo_offset, length as u64);
        // "The mmap() call doesn't fail if the mapping cannot be populated."
    }
    Ok(user_address)
}

pub fn sys_mprotect(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
    prot: u32,
) -> Result<(), Errno> {
    current_task.mm.protect(addr, length, mmap_prot_to_vm_opt(prot))?;
    Ok(())
}

pub fn sys_mremap(
    current_task: &CurrentTask,
    addr: UserAddress,
    old_length: usize,
    new_length: usize,
    flags: u32,
    new_addr: UserAddress,
) -> Result<UserAddress, Errno> {
    let flags = MremapFlags::from_bits(flags).ok_or_else(|| errno!(EINVAL))?;
    let addr =
        current_task.mm.remap(current_task, addr, old_length, new_length, flags, new_addr)?;
    Ok(addr)
}

pub fn sys_munmap(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
) -> Result<(), Errno> {
    current_task.mm.unmap(addr, length)?;
    Ok(())
}

pub fn sys_msync(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
    _flags: u32,
) -> Result<(), Errno> {
    not_implemented!(current_task, "msync not implemented");
    // Perform some basic validation of the address range given to satisfy gvisor tests that
    // use msync as a way to probe whether a page is mapped or not.
    current_task.mm.ensure_mapped(addr, length)?;
    Ok(())
}

pub fn sys_madvise(
    current_task: &CurrentTask,
    addr: UserAddress,
    length: usize,
    advice: u32,
) -> Result<(), Errno> {
    current_task.mm.madvise(current_task, addr, length, advice)?;
    Ok(())
}

pub fn sys_brk(current_task: &CurrentTask, addr: UserAddress) -> Result<UserAddress, Errno> {
    current_task.mm.set_brk(addr)
}

pub fn sys_process_vm_readv(
    current_task: &CurrentTask,
    pid: pid_t,
    local_iov_addr: UserAddress,
    local_iov_count: i32,
    remote_iov_addr: UserAddress,
    remote_iov_count: i32,
    _flags: usize,
) -> Result<usize, Errno> {
    let task = current_task.get_task(pid).ok_or_else(|| errno!(ESRCH))?;
    // When this check is loosened to allow reading memory from other processes, the check should
    // be like checking if the current process is allowed to debug the other process.
    if !Arc::ptr_eq(&task.thread_group, &current_task.thread_group) {
        return error!(EPERM);
    }
    let local_iov = task.mm.read_iovec(local_iov_addr, local_iov_count)?;
    let remote_iov = task.mm.read_iovec(remote_iov_addr, remote_iov_count)?;
    log_trace!(
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
        UserBuffer::get_total_length(&local_iov)?,
        UserBuffer::get_total_length(&remote_iov)?,
    );
    let mut buf = vec![0u8; len];
    let len = task.mm.read_all(&remote_iov, &mut buf)?;
    let len = task.mm.write_all(&local_iov, &buf[..len])?;
    Ok(len)
}

pub fn sys_membarrier(
    current_task: &CurrentTask,
    cmd: uapi::membarrier_cmd,
    flags: u32,
    cpu_id: i32,
) -> Result<u32, Errno> {
    // TODO(fxbug.dev/103867): This membarrier implementation does not do any real work.
    not_implemented!(
        current_task,
        "membarrier: cmd: 0x{:x}, flags: 0x{:x}, cpu_id: 0x{:x}",
        cmd,
        flags,
        cpu_id
    );
    match cmd {
        uapi::membarrier_cmd_MEMBARRIER_CMD_QUERY => Ok(0),
        uapi::membarrier_cmd_MEMBARRIER_CMD_PRIVATE_EXPEDITED_RSEQ => Ok(0),
        _ => error!(EINVAL),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::testing::*;

    #[::fuchsia::test]
    fn test_mmap_with_colliding_hint() {
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
            Ok(address) => {
                assert_ne!(address, mapped_address);
            }
            error => {
                panic!("mmap with colliding hint failed: {:?}", error);
            }
        }
    }

    #[::fuchsia::test]
    fn test_mmap_with_fixed_collision() {
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
            Ok(address) => {
                assert_eq!(address, mapped_address);
            }
            error => {
                panic!("mmap with fixed collision failed: {:?}", error);
            }
        }
    }

    #[::fuchsia::test]
    fn test_mmap_with_fixed_noreplace_collision() {
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
                panic!("mmap with fixed_noreplace collision failed: {:?}", result);
            }
        }
    }

    /// It is ok to call munmap with an address that is a multiple of the page size, and
    /// a non-zero length.
    #[::fuchsia::test]
    fn test_munmap() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, *PAGE_SIZE as usize), Ok(()));

        // Verify that the memory is no longer readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
    }

    /// It is ok to call munmap on an unmapped range.
    #[::fuchsia::test]
    fn test_munmap_not_mapped() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, *PAGE_SIZE as usize), Ok(()));
        assert_eq!(sys_munmap(&current_task, mapped_address, *PAGE_SIZE as usize), Ok(()));
    }

    /// It is an error to call munmap with a length of 0.
    #[::fuchsia::test]
    fn test_munmap_0_length() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, 0), error!(EINVAL));
    }

    /// It is an error to call munmap with an address that is not a multiple of the page size.
    #[::fuchsia::test]
    fn test_munmap_not_aligned() {
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
    #[::fuchsia::test]
    fn test_munmap_unmap_partial() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&current_task, mapped_address, (*PAGE_SIZE as usize) / 2), Ok(()));

        // Verify that memory can't be read in either half of the page.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + (*PAGE_SIZE - 2), &mut data),
            error!(EFAULT)
        );
    }

    /// All pages that intersect the munmap range should be unmapped.
    #[::fuchsia::test]
    fn test_munmap_multiple_pages() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&current_task, mapped_address, (*PAGE_SIZE as usize) + 1), Ok(()));

        // Verify that neither page is readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data),
            error!(EFAULT)
        );
    }

    /// Only the pages that intersect the munmap range should be unmapped.
    #[::fuchsia::test]
    fn test_munmap_one_of_many_pages() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&current_task, mapped_address, (*PAGE_SIZE as usize) - 1), Ok(()));

        // Verify that the second page is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            current_task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data),
            Ok(())
        );
    }

    /// Unmap the middle page of a mapping.
    #[::fuchsia::test]
    fn test_munmap_middle_page() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_address = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        assert_eq!(
            sys_munmap(&current_task, mapped_address + *PAGE_SIZE, *PAGE_SIZE as usize),
            Ok(())
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
    #[::fuchsia::test]
    fn test_munmap_many_mappings() {
        let (_kernel, current_task) = create_kernel_and_task();

        let mapped_addresses: Vec<_> = std::iter::repeat_with(|| {
            map_memory(&current_task, UserAddress::default(), *PAGE_SIZE)
        })
        .take(3)
        .collect();
        let min_address = *mapped_addresses.iter().min().unwrap();
        let max_address = *mapped_addresses.iter().max().unwrap();
        let unmap_length = (max_address - min_address) + *PAGE_SIZE as usize;

        assert_eq!(sys_munmap(&current_task, min_address, unmap_length), Ok(()));

        // Verify that none of the mapped pages are readable.
        let mut data: [u8; 5] = [0; 5];
        for mapped_address in mapped_addresses {
            assert_eq!(current_task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        }
    }

    #[::fuchsia::test]
    fn test_msync_validates_address_range() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 3 pages and test that ranges covering these pages return no error.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 3, 0), Ok(()));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 2, 0), Ok(()));
        assert_eq!(sys_msync(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize * 2, 0), Ok(()));

        // Unmap the middle page and test that ranges covering that page return ENOMEM.
        sys_munmap(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize).expect("unmap middle");
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize, 0), Ok(()));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 3, 0), error!(ENOMEM));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 2, 0), error!(ENOMEM));
        assert_eq!(
            sys_msync(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize * 2, 0),
            error!(ENOMEM)
        );
        assert_eq!(sys_msync(&current_task, addr + *PAGE_SIZE * 2, *PAGE_SIZE as usize, 0), Ok(()));

        // Map the middle page back and test that ranges covering the three pages
        // (spanning multiple ranges) return no error.
        assert_eq!(map_memory(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE), addr + *PAGE_SIZE);
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 3, 0), Ok(()));
        assert_eq!(sys_msync(&current_task, addr, *PAGE_SIZE as usize * 2, 0), Ok(()));
        assert_eq!(sys_msync(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize * 2, 0), Ok(()));
    }

    /// Shrinks an entire range.
    #[::fuchsia::test]
    fn test_mremap_shrink_whole_range_from_end() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 2 pages.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);
        fill_page(&current_task, addr, 'a');
        fill_page(&current_task, addr + *PAGE_SIZE, 'b');

        // Shrink the mapping from 2 to 1 pages.
        assert_eq!(
            remap_memory(
                &current_task,
                addr,
                *PAGE_SIZE * 2,
                *PAGE_SIZE,
                0,
                UserAddress::default()
            ),
            Ok(addr)
        );

        check_page_eq(&current_task, addr, 'a');
        check_unmapped(&current_task, addr + *PAGE_SIZE);
    }

    /// Shrinks part of a range, introducing a hole in the middle.
    #[::fuchsia::test]
    fn test_mremap_shrink_partial_range() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 3 pages.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        fill_page(&current_task, addr, 'a');
        fill_page(&current_task, addr + *PAGE_SIZE, 'b');
        fill_page(&current_task, addr + *PAGE_SIZE * 2, 'c');

        // Shrink the first 2 pages down to 1, creating a hole.
        assert_eq!(
            remap_memory(
                &current_task,
                addr,
                *PAGE_SIZE * 2,
                *PAGE_SIZE,
                0,
                UserAddress::default()
            ),
            Ok(addr)
        );

        check_page_eq(&current_task, addr, 'a');
        check_unmapped(&current_task, addr + *PAGE_SIZE);
        check_page_eq(&current_task, addr + *PAGE_SIZE * 2, 'c');
    }

    /// Shrinking doesn't care if the range specified spans multiple mappings.
    #[::fuchsia::test]
    fn test_mremap_shrink_across_ranges() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 3 pages, unmap the middle, then map the middle again. This will leave us with
        // 3 contiguous mappings.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        assert_eq!(sys_munmap(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize), Ok(()));
        assert_eq!(map_memory(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE), addr + *PAGE_SIZE);

        fill_page(&current_task, addr, 'a');
        fill_page(&current_task, addr + *PAGE_SIZE, 'b');
        fill_page(&current_task, addr + *PAGE_SIZE * 2, 'c');

        // Remap over all three mappings, shrinking to 1 page.
        assert_eq!(
            remap_memory(
                &current_task,
                addr,
                *PAGE_SIZE * 3,
                *PAGE_SIZE,
                0,
                UserAddress::default()
            ),
            Ok(addr)
        );

        check_page_eq(&current_task, addr, 'a');
        check_unmapped(&current_task, addr + *PAGE_SIZE);
        check_unmapped(&current_task, addr + *PAGE_SIZE * 2);
    }

    /// Grows a mapping in-place.
    #[::fuchsia::test]
    fn test_mremap_grow_in_place() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 3 pages, unmap the middle, leaving a hole.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        fill_page(&current_task, addr, 'a');
        fill_page(&current_task, addr + *PAGE_SIZE, 'b');
        fill_page(&current_task, addr + *PAGE_SIZE * 2, 'c');
        assert_eq!(sys_munmap(&current_task, addr + *PAGE_SIZE, *PAGE_SIZE as usize), Ok(()));

        // Grow the first page in-place into the middle.
        assert_eq!(
            remap_memory(
                &current_task,
                addr,
                *PAGE_SIZE,
                *PAGE_SIZE * 2,
                0,
                UserAddress::default()
            ),
            Ok(addr)
        );

        check_page_eq(&current_task, addr, 'a');

        // The middle page should be new, and not just pointing to the original middle page filled
        // with 'b'.
        check_page_ne(&current_task, addr + *PAGE_SIZE, 'b');

        check_page_eq(&current_task, addr + *PAGE_SIZE * 2, 'c');
    }

    /// Tries to grow a set of pages that cannot fit, and forces a move.
    #[::fuchsia::test]
    fn test_mremap_grow_maymove() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 3 pages.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        fill_page(&current_task, addr, 'a');
        fill_page(&current_task, addr + *PAGE_SIZE, 'b');
        fill_page(&current_task, addr + *PAGE_SIZE * 2, 'c');

        // Grow the first two pages by 1, forcing a move.
        let new_addr = remap_memory(
            &current_task,
            addr,
            *PAGE_SIZE * 2,
            *PAGE_SIZE * 3,
            MREMAP_MAYMOVE,
            UserAddress::default(),
        )
        .expect("failed to mremap");

        assert_ne!(new_addr, addr, "mremap did not move the mapping");

        // The first two pages should have been moved.
        check_unmapped(&current_task, addr);
        check_unmapped(&current_task, addr + *PAGE_SIZE);

        // The third page should still be present.
        check_page_eq(&current_task, addr + *PAGE_SIZE * 2, 'c');

        // The moved pages should have the same contents.
        check_page_eq(&current_task, new_addr, 'a');
        check_page_eq(&current_task, new_addr + *PAGE_SIZE, 'b');

        // The newly grown page should not be the same as the original third page.
        check_page_ne(&current_task, new_addr + *PAGE_SIZE * 2, 'c');
    }

    /// Shrinks a set of pages and move them to a fixed location.
    #[::fuchsia::test]
    fn test_mremap_shrink_fixed() {
        let (_kernel, current_task) = create_kernel_and_task();

        // Map 2 pages which will act as the destination.
        let dst_addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);
        fill_page(&current_task, dst_addr, 'y');
        fill_page(&current_task, dst_addr + *PAGE_SIZE, 'z');

        // Map 3 pages.
        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);
        fill_page(&current_task, addr, 'a');
        fill_page(&current_task, addr + *PAGE_SIZE, 'b');
        fill_page(&current_task, addr + *PAGE_SIZE * 2, 'c');

        // Shrink the first two pages and move them to overwrite the mappings at `dst_addr`.
        let new_addr = remap_memory(
            &current_task,
            addr,
            *PAGE_SIZE * 2,
            *PAGE_SIZE,
            MREMAP_MAYMOVE | MREMAP_FIXED,
            dst_addr,
        )
        .expect("failed to mremap");

        assert_eq!(new_addr, dst_addr, "mremap did not move the mapping");

        // The first two pages should have been moved.
        check_unmapped(&current_task, addr);
        check_unmapped(&current_task, addr + *PAGE_SIZE);

        // The third page should still be present.
        check_page_eq(&current_task, addr + *PAGE_SIZE * 2, 'c');

        // The first moved page should have the same contents.
        check_page_eq(&current_task, new_addr, 'a');

        // The second page should be part of the original dst mapping.
        check_page_eq(&current_task, new_addr + *PAGE_SIZE, 'z');
    }

    #[::fuchsia::test]
    fn test_map_32_bit() {
        let (_kernel, current_task) = create_kernel_and_task();
        let page_size = *PAGE_SIZE;

        for _i in 0..256 {
            match sys_mmap(
                &current_task,
                UserAddress::from(0),
                page_size as usize,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT,
                FdNumber::from_raw(-1),
                0,
            ) {
                Ok(address) => {
                    let memory_end = address.ptr() + page_size as usize;
                    assert!(memory_end <= 0x80000000);
                }
                error => {
                    panic!("mmap with MAP_32BIT failed: {:?}", error);
                }
            }
        }
    }

    #[::fuchsia::test]
    fn test_membarrier() {
        let (_kernel, current_task) = create_kernel_and_task();
        assert_eq!(sys_membarrier(&current_task, 0, 0, 0), Ok(0));
        assert_eq!(sys_membarrier(&current_task, 3, 0, 0), error!(EINVAL));
    }
}
