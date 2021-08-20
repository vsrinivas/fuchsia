// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef};
use std::ffi::CStr;
use std::sync::Arc;

use crate::errno;
use crate::error;
use crate::fs::*;
use crate::logging::*;
use crate::mm::*;
use crate::not_implemented;
use crate::syscalls::*;
use crate::types::*;
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
    ctx: &SyscallContext<'_>,
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
    if flags & MAP_FIXED != 0 {
        // SAFETY: We are operating on another process, so it's safe to use SPECIFIC_OVERWRITE
        zx_flags |= unsafe {
            zx::VmarFlags::from_bits_unchecked(zx::VmarFlagsExtended::SPECIFIC_OVERWRITE.bits())
        };
    }

    let vmo = if flags & MAP_ANONYMOUS != 0 {
        let mut vmo = zx::Vmo::create(length as u64).map_err(|s| match s {
            zx::Status::NO_MEMORY => errno!(ENOMEM),
            zx::Status::OUT_OF_RANGE => errno!(ENOMEM),
            _ => impossible_error(s),
        })?;
        vmo.set_name(CStr::from_bytes_with_nul(b"starnix-anon\0").unwrap())
            .map_err(impossible_error)?;
        if zx_flags.contains(zx::VmarFlags::PERM_EXECUTE) {
            vmo = vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
        }
        vmo
    } else {
        // TODO(tbodt): maximize protection flags so that mprotect works
        let file = ctx.task.files.get(fd)?;
        let zx_prot = mmap_prot_to_vm_opt(prot);
        if flags & MAP_PRIVATE != 0 {
            // TODO(tbodt): Use VMO_FLAG_PRIVATE to have the filesystem server do the clone for us.
            let vmo = file.get_vmo(&ctx.task, zx_prot - zx::VmarFlags::PERM_WRITE)?;
            let mut clone_flags = zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE;
            if !zx_prot.contains(zx::VmarFlags::PERM_WRITE) {
                clone_flags |= zx::VmoChildOptions::NO_WRITE;
            }
            vmo.create_child(clone_flags, 0, vmo.get_size().map_err(impossible_error)?)
                .map_err(impossible_error)?
        } else {
            file.get_vmo(&ctx.task, zx_prot)?
        }
    };

    let vmo = Arc::new(vmo);
    let vmo_offset = if flags & MAP_ANONYMOUS != 0 { 0 } else { offset };

    let mut options = MappingOptions::empty();
    if flags & MAP_SHARED != 0 {
        options |= MappingOptions::SHARED;
    }

    let try_map =
        |addr, flags| ctx.task.mm.map(addr, Arc::clone(&vmo), vmo_offset, length, flags, options);
    let addr = match try_map(addr, zx_flags) {
        Err(errno)
            if errno == EINVAL
                && zx_flags.contains(zx::VmarFlags::SPECIFIC)
                && (flags & MAP_FIXED == 0) =>
        {
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
    ctx: &SyscallContext<'_>,
    addr: UserAddress,
    length: usize,
    prot: u32,
) -> Result<SyscallResult, Errno> {
    ctx.task.mm.protect(addr, length, mmap_prot_to_vm_opt(prot))?;
    Ok(SUCCESS)
}

pub fn sys_munmap(
    ctx: &SyscallContext<'_>,
    addr: UserAddress,
    length: usize,
) -> Result<SyscallResult, Errno> {
    ctx.task.mm.unmap(addr, length)?;
    Ok(SUCCESS)
}

pub fn sys_msync(
    _ctx: &SyscallContext<'_>,
    _addr: UserAddress,
    _length: usize,
    _flags: u32,
) -> Result<SyscallResult, Errno> {
    not_implemented!("msync not implemented");
    Ok(SUCCESS)
}

pub fn sys_brk(ctx: &SyscallContext<'_>, addr: UserAddress) -> Result<SyscallResult, Errno> {
    Ok(ctx.task.mm.set_brk(addr)?.into())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    use crate::testing::*;

    /// It is ok to call munmap with an address that is a multiple of the page size, and
    /// a non-zero length.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));

        // Verify that the memory is no longer readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
    }

    /// It is ok to call munmap on an unmapped range.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_mapped() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
        assert_eq!(sys_munmap(&ctx, mapped_address, *PAGE_SIZE as usize), Ok(SUCCESS));
    }

    /// It is an error to call munmap with a length of 0.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_0_length() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, 0), error!(EINVAL));
    }

    /// It is an error to call munmap with an address that is not a multiple of the page size.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_not_aligned() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address + 1u64, *PAGE_SIZE as usize), error!(EINVAL));

        // Verify that the memory is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), Ok(()));
    }

    /// The entire page should be unmapped, not just the range [address, address + length).
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_unmap_partial() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE);
        assert_eq!(sys_munmap(&ctx, mapped_address, (*PAGE_SIZE as usize) / 2), Ok(SUCCESS));

        // Verify that memory can't be read in either half of the page.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            ctx.task.mm.read_memory(mapped_address + (*PAGE_SIZE - 2), &mut data),
            error!(EFAULT)
        );
    }

    /// All pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_multiple_pages() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&ctx, mapped_address, (*PAGE_SIZE as usize) + 1), Ok(SUCCESS));

        // Verify that neither page is readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(
            ctx.task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data),
            error!(EFAULT)
        );
    }

    /// Only the pages that intersect the munmap range should be unmapped.
    #[fasync::run_singlethreaded(test)]
    async fn test_munmap_one_of_many_pages() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let ctx = SyscallContext::new(&task_owner.task);

        let mapped_address = map_memory(&ctx, UserAddress::default(), *PAGE_SIZE * 2);
        assert_eq!(sys_munmap(&ctx, mapped_address, (*PAGE_SIZE as usize) - 1), Ok(SUCCESS));

        // Verify that the second page is still readable.
        let mut data: [u8; 5] = [0; 5];
        assert_eq!(ctx.task.mm.read_memory(mapped_address, &mut data), error!(EFAULT));
        assert_eq!(ctx.task.mm.read_memory(mapped_address + *PAGE_SIZE + 1u64, &mut data), Ok(()));
    }
}
