// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitflags::bitflags;
use fuchsia_zircon::{self as zx, AsHandleRef};
use lazy_static::lazy_static;
use parking_lot::{Mutex, RwLock};
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::convert::TryInto;
use std::ffi::{CStr, CString};
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

use crate::collections::*;
use crate::fs::*;
use crate::logging::*;
use crate::mm::vmo::round_up_to_system_page_size;
use crate::mm::FutexTable;
use crate::task::{CurrentTask, EventHandler, Task, Waiter};
use crate::types::{range_ext::RangeExt, *};
use crate::vmex_resource::VMEX_RESOURCE;
use crate::{errno, error, fd_impl_nonblocking, fd_impl_seekable, mode, not_implemented};

lazy_static! {
    pub static ref PAGE_SIZE: u64 = zx::system_get_page_size() as u64;
}

bitflags! {
    pub struct MappingOptions: u32 {
      const SHARED = 1;
      const ANONYMOUS = 2;
    }
}

bitflags! {
    pub struct MremapFlags: u32 {
        const MAYMOVE = MREMAP_MAYMOVE;
        const FIXED = MREMAP_FIXED;
        const DONTUNMAP = MREMAP_DONTUNMAP;
    }
}

#[derive(Debug, Eq, PartialEq, Clone)]
struct Mapping {
    /// The base address of this mapping.
    ///
    /// Keep in mind that the mapping might be trimmed in the RangeMap if the
    /// part of the mapping is unmapped, which means the base might extend
    /// before the currently valid portion of the mapping.
    base: UserAddress,

    /// The VMO that contains the memory used in this mapping.
    vmo: Arc<zx::Vmo>,

    /// The offset in the VMO that corresponds to the base address.
    vmo_offset: u64,

    /// The rights used by the mapping.
    permissions: zx::VmarFlags,

    /// The flags for this mapping.
    options: MappingOptions,

    /// The name of the file used for the mapping. None if the mapping is anonymous.
    filename: Option<NamespaceNode>,

    /// A name associated with the mapping. Set by prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ...).
    name: CString,
}

impl Mapping {
    fn new(
        base: UserAddress,
        vmo: Arc<zx::Vmo>,
        vmo_offset: u64,
        flags: zx::VmarFlags,
        options: MappingOptions,
    ) -> Mapping {
        Mapping {
            base,
            vmo,
            vmo_offset,
            permissions: flags
                & (zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::PERM_EXECUTE),
            options,
            filename: None,
            name: CString::default(),
        }
    }

    fn with_flags(&self, flags: zx::VmarFlags) -> Mapping {
        Mapping {
            base: self.base,
            vmo: self.vmo.clone(),
            vmo_offset: self.vmo_offset,
            permissions: flags
                & (zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::PERM_EXECUTE),
            options: self.options,
            filename: self.filename.clone(),
            name: self.name.clone(),
        }
    }
}

const PROGRAM_BREAK_LIMIT: u64 = 64 * 1024 * 1024;

#[derive(Debug, Default, Clone, Copy, Eq, PartialEq)]
struct ProgramBreak {
    // These base address at which the data segment is mapped.
    base: UserAddress,

    // The current program break.
    //
    // The addresses from [base, current.round_up(*PAGE_SIZE)) are mapped into the
    // client address space from the underlying |vmo|.
    current: UserAddress,
}

/// The policy about whether the address space can be dumped.
#[derive(Debug, Clone, Copy, Eq, PartialEq)]
pub enum DumpPolicy {
    /// The address space cannot be dumped.
    ///
    /// Corresponds to SUID_DUMP_DISABLE.
    DISABLE,

    /// The address space can be dumped.
    ///
    /// Corresponds to SUID_DUMP_USER.
    USER,
}
pub struct MemoryManagerState {
    /// The VMAR in which userspace mappings occur.
    ///
    /// We map userspace memory in this child VMAR so that we can destroy the
    /// entire VMAR during exec.
    user_vmar: zx::Vmar,

    /// Cached VmarInfo for user_vmar.
    user_vmar_info: zx::VmarInfo,

    /// State for the brk and sbrk syscalls.
    brk: Option<ProgramBreak>,

    /// The memory mappings currently used by this address space.
    ///
    /// The mappings record which VMO backs each address.
    mappings: RangeMap<UserAddress, Mapping>,

    /// The namespace node that represents the executable associated with this task.
    executable_node: Option<NamespaceNode>,

    /// Stack location and size
    pub stack_base: UserAddress,
    pub stack_size: usize,
}

impl MemoryManagerState {
    fn map(
        &mut self,
        vmar_offset: usize,
        vmo: Arc<zx::Vmo>,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        options: MappingOptions,
        filename: Option<NamespaceNode>,
    ) -> Result<UserAddress, Errno> {
        let addr = UserAddress::from_ptr(
            self.user_vmar
                .map(vmar_offset, &vmo, vmo_offset, length, flags)
                .map_err(MemoryManager::get_errno_for_map_err)?,
        );
        let mut mapping = Mapping::new(addr, vmo, vmo_offset, flags, options);
        mapping.filename = filename;
        let end = (addr + length).round_up(*PAGE_SIZE)?;
        self.mappings.insert(addr..end, mapping);
        Ok(addr)
    }

    fn remap(
        &mut self,
        old_addr: UserAddress,
        old_length: usize,
        new_length: usize,
        flags: MremapFlags,
        new_address: UserAddress,
    ) -> Result<UserAddress, Errno> {
        // MREMAP_FIXED moves a mapping, which requires MREMAP_MAYMOVE.
        if flags.contains(MremapFlags::FIXED) && !flags.contains(MremapFlags::MAYMOVE) {
            return error!(EINVAL);
        }

        // MREMAP_DONTUNMAP is always a move to a specific address,
        // which requires MREMAP_FIXED. There is no resizing allowed either.
        if flags.contains(MremapFlags::DONTUNMAP)
            && (!flags.contains(MremapFlags::FIXED) || old_length != new_length)
        {
            return error!(EINVAL);
        }

        // In-place copies are invalid.
        if !flags.contains(MremapFlags::MAYMOVE) && old_length == 0 {
            return error!(ENOMEM);
        }

        // TODO(fxbug.dev/88262): Implement support for MREMAP_DONTUNMAP.
        if flags.contains(MremapFlags::DONTUNMAP) {
            not_implemented!("mremap flag MREMAP_DONTUNMAP not implemented");
            return error!(EOPNOTSUPP);
        }

        if new_length == 0 {
            return error!(EINVAL);
        }

        // Make sure old_addr is page-aligned.
        if !old_addr.is_aligned(*PAGE_SIZE) {
            return error!(EINVAL);
        }

        let old_length = round_up_to_system_page_size(old_length)?;
        let new_length = round_up_to_system_page_size(new_length)?;

        if !flags.contains(MremapFlags::FIXED) && old_length != 0 {
            // We are not requested to remap to a specific address, so first we see if we can remap
            // in-place. In-place copies (old_length == 0) are not allowed.
            if let Some(new_address) = self.try_remap_in_place(old_addr, old_length, new_length)? {
                return Ok(new_address);
            }
        }

        // There is no space to grow in place, or there is an explicit request to move.
        if flags.contains(MremapFlags::MAYMOVE) {
            let dst_address =
                if flags.contains(MremapFlags::FIXED) { Some(new_address) } else { None };
            self.remap_move(old_addr, old_length, dst_address, new_length)
        } else {
            error!(ENOMEM)
        }
    }

    /// Attempts to grow or shrink the mapping in-place. Returns `Ok(Some(addr))` if the remap was
    /// successful. Returns `Ok(None)` if there was no space to grow.
    fn try_remap_in_place(
        &mut self,
        old_addr: UserAddress,
        old_length: usize,
        new_length: usize,
    ) -> Result<Option<UserAddress>, Errno> {
        let old_range = old_addr..old_addr.checked_add(old_length).ok_or_else(|| errno!(EINVAL))?;
        let new_range_in_place =
            old_addr..old_addr.checked_add(new_length).ok_or_else(|| errno!(EINVAL))?;

        if new_length <= old_length {
            // Shrink the mapping in-place, which should always succeed.
            // This is done by unmapping the extraneous region.
            if new_length != old_length {
                self.unmap(new_range_in_place.end, old_length - new_length)?;
            }
            return Ok(Some(old_addr));
        }

        if self.mappings.intersection(old_range.end..new_range_in_place.end).next().is_some() {
            // There is some mapping in the growth range prevening an in-place growth.
            return Ok(None);
        }

        // There is space to grow in-place. The old range must be one contiguous mapping.
        let (original_range, mapping) = self.mappings.get(&old_addr).ok_or(errno!(EINVAL))?;
        if old_range.end > original_range.end {
            return error!(EFAULT);
        }
        let original_range = original_range.clone();
        let original_mapping = mapping.clone();

        // Compute the new length of the entire mapping once it has grown.
        let final_length = (original_range.end - original_range.start) + (new_length - old_length);

        if original_mapping.options.contains(MappingOptions::ANONYMOUS)
            && !original_mapping.options.contains(MappingOptions::SHARED)
        {
            // As a special case for private, anonymous mappings, allocate more space in the
            // VMO. FD-backed mappings have their backing memory handled by the file system.
            let new_vmo_size = original_mapping
                .vmo_offset
                .checked_add(final_length as u64)
                .ok_or(errno!(EINVAL))?;
            original_mapping
                .vmo
                .set_size(new_vmo_size)
                .map_err(MemoryManager::get_errno_for_map_err)?;
            // Zero-out the pages that were added when growing. This is not necessary, but ensures
            // correctness of our COW implementation. Ignore any errors.
            let original_length = original_range.end - original_range.start;
            let _ = original_mapping.vmo.op_range(
                zx::VmoOp::ZERO,
                original_mapping.vmo_offset + original_length as u64,
                (final_length - original_length) as u64,
            );
        }

        // Since the mapping is growing in-place, it must be mapped at the original address.
        let vmar_flags = original_mapping.permissions
            | unsafe {
                zx::VmarFlags::from_bits_unchecked(zx::VmarFlagsExtended::SPECIFIC_OVERWRITE.bits())
            };

        // Re-map the original range, which may include pages before the requested range.
        Ok(Some(self.map(
            (original_range.start - self.user_vmar_info.base).ptr(),
            original_mapping.vmo,
            original_mapping.vmo_offset,
            final_length,
            vmar_flags,
            original_mapping.options,
            original_mapping.filename,
        )?))
    }

    /// Grows or shrinks the mapping while moving it to a new destination. If `dst_addr` is `None`,
    /// the kernel decides where to move the mapping.
    fn remap_move(
        &mut self,
        src_addr: UserAddress,
        src_length: usize,
        dst_addr: Option<UserAddress>,
        dst_length: usize,
    ) -> Result<UserAddress, Errno> {
        let src_range = src_addr..src_addr.checked_add(src_length).ok_or_else(|| errno!(EINVAL))?;
        let (original_range, src_mapping) = self.mappings.get(&src_addr).ok_or(errno!(EINVAL))?;
        let original_range = original_range.clone();
        let src_mapping = src_mapping.clone();

        if src_length == 0 && !src_mapping.options.contains(MappingOptions::SHARED) {
            // src_length == 0 means that the mapping is to be copied. This behavior is only valid
            // with MAP_SHARED mappings.
            return error!(EINVAL);
        }

        let offset_into_original_range = (src_addr - original_range.start) as u64;
        let dst_vmo_offset = src_mapping.vmo_offset + offset_into_original_range;

        if let Some(dst_addr) = &dst_addr {
            // The mapping is being moved to a specific address.
            let dst_range =
                *dst_addr..(dst_addr.checked_add(dst_length).ok_or_else(|| errno!(EINVAL))?);
            if src_range.intersects(&dst_range) {
                return error!(EINVAL);
            }

            // If the destination range is smaller than the source range, we must first shrink
            // the source range in place. This must be done now and visible to processes, even if
            // a later failure causes the remap operation to fail.
            if src_length != 0 && src_length > dst_length {
                self.unmap(src_addr + dst_length, src_length - dst_length)?;
            }

            // The destination range must be unmapped. This must be done now and visible to
            // processes, even if a later failure causes the remap operation to fail.
            self.unmap(*dst_addr, dst_length)?;
        }

        if src_range.end > original_range.end {
            // The source range is not one contiguous mapping. This check must be done only after
            // the source range is shrunk and the destination unmapped.
            return error!(EFAULT);
        }

        // Get the destination address, which may be 0 if we are letting the kernel choose for us.
        let (vmar_offset, vmar_flags) = if let Some(dst_addr) = &dst_addr {
            (
                (*dst_addr - self.user_vmar_info.base).ptr(),
                src_mapping.permissions | zx::VmarFlags::SPECIFIC,
            )
        } else {
            (0, src_mapping.permissions)
        };

        let new_address = if src_mapping.options.contains(MappingOptions::ANONYMOUS)
            && !src_mapping.options.contains(MappingOptions::SHARED)
        {
            // This mapping is a private, anonymous mapping. Create a COW child VMO that covers
            // the pages being moved and map that into the destination.
            let child_vmo = src_mapping
                .vmo
                .create_child(
                    zx::VmoChildOptions::SNAPSHOT | zx::VmoChildOptions::RESIZABLE,
                    dst_vmo_offset,
                    dst_length as u64,
                )
                .map_err(MemoryManager::get_errno_for_map_err)?;
            if dst_length > src_length {
                // The mapping has grown. Zero-out the pages that were "added" when growing the
                // mapping. These pages might be pointing inside the parent VMO, in which case
                // we want to zero them out to make them look like new pages. Since this is a COW
                // child VMO, this will simply allocate new pages.
                // This is not necessary, but ensures correctness of our COW implementation.
                // Ignore any errors.
                let _ = child_vmo.op_range(
                    zx::VmoOp::ZERO,
                    src_length as u64,
                    (dst_length - src_length) as u64,
                );
            }
            self.map(
                vmar_offset,
                Arc::new(child_vmo),
                0,
                dst_length,
                vmar_flags,
                src_mapping.options,
                src_mapping.filename,
            )?
        } else {
            // This mapping is backed by an FD, just map the range of the VMO covering the moved
            // pages. If the VMO already had COW semantics, this preserves them.
            self.map(
                vmar_offset,
                src_mapping.vmo,
                dst_vmo_offset,
                dst_length,
                vmar_flags,
                src_mapping.options,
                src_mapping.filename,
            )?
        };

        if src_length != 0 {
            // Only unmap the source range if this is not a copy. It was checked earlier that
            // this mapping is MAP_SHARED.
            self.unmap(src_addr, src_length)?;
        }

        Ok(new_address)
    }

    // The range to unmap can span multiple mappings, and can split mappings if
    // the range start or end falls in the middle of a mapping.
    //
    // For example, with this set of mappings and unmap range `R`:
    //
    //   [  A  ][ B ] [    C    ]     <- mappings
    //      |-------------|           <- unmap range R
    //
    // Assuming the mappings are all MAP_ANONYMOUS:
    // - the pages of A, B, and C that fall in range R are unmapped; the VMO backing B is dropped.
    // - the VMO backing A is shrunk.
    // - a COW child VMO is created from C, which is mapped in the range of C that falls outside R.
    //
    // File-backed mappings don't need to have their VMOs modified.
    fn unmap(&mut self, addr: UserAddress, length: usize) -> Result<(), Errno> {
        if !addr.is_aligned(*PAGE_SIZE) {
            return error!(EINVAL);
        }
        let length = round_up_to_system_page_size(length)?;
        if length == 0 {
            return error!(EINVAL);
        }
        let end_addr = addr.checked_add(length).ok_or(errno!(EINVAL))?;
        let mut unmap_length = length;

        // Find the private, anonymous mapping that will get its tail cut off by this unmap call.
        let truncated_head = match self.mappings.get(&addr) {
            Some((range, mapping))
                if range.start != addr
                    && mapping.options.contains(MappingOptions::ANONYMOUS)
                    && !mapping.options.contains(MappingOptions::SHARED) =>
            {
                Some((range.start..addr, mapping.clone()))
            }
            _ => None,
        };

        // Find the private, anonymous mapping that will get its head cut off by this unmap call.
        let truncated_tail = match self.mappings.get(&end_addr) {
            Some((range, mapping))
                if range.end != end_addr
                    && mapping.options.contains(MappingOptions::ANONYMOUS)
                    && !mapping.options.contains(MappingOptions::SHARED) =>
            {
                // We are going to make a child VMO of the remaining mapping and remap
                // it, so we increase the range of the unmap call to include the tail.
                unmap_length = range.end - addr;
                Some((end_addr..range.end, mapping.clone()))
            }
            _ => None,
        };

        // Actually unmap the range, including the the tail of any range that would have been split.
        // This operation is safe because we're operating on another process.
        match unsafe { self.user_vmar.unmap(addr.ptr(), unmap_length) } {
            Ok(_) => Ok(()),
            Err(zx::Status::NOT_FOUND) => Ok(()),
            Err(zx::Status::INVALID_ARGS) => error!(EINVAL),
            Err(status) => Err(impossible_error(status)),
        }?;

        // Remove the original range of mappings from our map.
        self.mappings.remove(&(addr..end_addr));

        if let Some((range, mapping)) = truncated_tail {
            // Create and map a child COW VMO mapping that represents the truncated tail.
            let vmo_info = mapping.vmo.basic_info().map_err(impossible_error)?;
            let child_vmo_offset = (range.start - mapping.base) as u64 + mapping.vmo_offset;
            let child_length = range.end - range.start;
            let mut child_vmo = mapping
                .vmo
                .create_child(
                    zx::VmoChildOptions::SNAPSHOT | zx::VmoChildOptions::RESIZABLE,
                    child_vmo_offset,
                    child_length as u64,
                )
                .map_err(MemoryManager::get_errno_for_map_err)?;
            if vmo_info.rights.contains(zx::Rights::EXECUTE) {
                child_vmo =
                    child_vmo.replace_as_executable(&VMEX_RESOURCE).map_err(impossible_error)?;
            }
            self.map(
                (range.start - self.user_vmar_info.base).ptr(),
                Arc::new(child_vmo),
                0,
                child_length,
                mapping.permissions | zx::VmarFlags::SPECIFIC,
                mapping.options,
                mapping.filename,
            )?;
        }

        if let Some((range, mapping)) = truncated_head {
            // Resize the VMO of the head mapping, whose tail was cut off.
            let new_mapping_size = (range.end - range.start) as u64;
            let new_vmo_size = mapping.vmo_offset + new_mapping_size;
            mapping.vmo.set_size(new_vmo_size).map_err(MemoryManager::get_errno_for_map_err)?;
        }

        Ok(())
    }

    fn protect(
        &mut self,
        addr: UserAddress,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<(), Errno> {
        let (_, mapping) = self.mappings.get(&addr).ok_or(errno!(EINVAL))?;
        let mapping = mapping.with_flags(flags);

        // SAFETY: This is safe because the vmar belongs to a different process.
        unsafe { self.user_vmar.protect(addr.ptr(), length, flags) }.map_err(|s| match s {
            zx::Status::INVALID_ARGS => errno!(EINVAL),
            // TODO: This should still succeed and change protection on whatever is mapped.
            zx::Status::NOT_FOUND => errno!(EINVAL),
            zx::Status::ACCESS_DENIED => errno!(EACCES),
            _ => impossible_error(s),
        })?;

        let end = (addr + length).round_up(*PAGE_SIZE)?;
        self.mappings.insert(addr..end, mapping);
        Ok(())
    }

    fn max_address(&self) -> UserAddress {
        UserAddress::from_ptr(self.user_vmar_info.base + self.user_vmar_info.len)
    }
}

fn create_user_vmar(vmar: &zx::Vmar, vmar_info: &zx::VmarInfo) -> Result<zx::Vmar, zx::Status> {
    let (vmar, ptr) = vmar.allocate(
        0,
        vmar_info.len,
        zx::VmarFlags::SPECIFIC
            | zx::VmarFlags::CAN_MAP_SPECIFIC
            | zx::VmarFlags::CAN_MAP_READ
            | zx::VmarFlags::CAN_MAP_WRITE
            | zx::VmarFlags::CAN_MAP_EXECUTE,
    )?;
    assert_eq!(ptr, vmar_info.base);
    Ok(vmar)
}

pub struct MemoryManager {
    /// A handle to the underlying Zircon process object.
    // TODO: Remove this handle once we can read and write process memory directly.
    process: zx::Process,

    /// The root VMAR for the child process.
    ///
    /// Instead of mapping memory directly in this VMAR, we map the memory in
    /// `state.user_vmar`.
    root_vmar: zx::Vmar,

    /// The base address of the root_vmar.
    pub base_addr: UserAddress,

    /// The futexes in this address space.
    pub futex: FutexTable,

    /// Mutable state for the memory manager.
    pub state: RwLock<MemoryManagerState>,

    /// Whether this address space is dumpable.
    pub dumpable: Mutex<DumpPolicy>,
}

impl MemoryManager {
    pub fn new(process: zx::Process, root_vmar: zx::Vmar) -> Result<Self, zx::Status> {
        let info = root_vmar.info()?;
        let user_vmar = create_user_vmar(&root_vmar, &info)?;
        let user_vmar_info = user_vmar.info()?;
        Ok(MemoryManager {
            process,
            root_vmar,
            base_addr: UserAddress::from_ptr(info.base),
            futex: FutexTable::default(),
            state: RwLock::new(MemoryManagerState {
                user_vmar,
                user_vmar_info,
                brk: None,
                mappings: RangeMap::new(),
                executable_node: None,
                stack_base: UserAddress::default(),
                stack_size: 0,
            }),
            dumpable: Mutex::new(DumpPolicy::DISABLE),
        })
    }

    pub fn set_brk(&self, addr: UserAddress) -> Result<UserAddress, Errno> {
        let mut state = self.state.write();

        // Ensure that a program break exists by mapping at least one page.
        let mut brk = match state.brk {
            None => {
                let vmo = zx::Vmo::create(PROGRAM_BREAK_LIMIT).map_err(|_| errno!(ENOMEM))?;
                vmo.set_name(CStr::from_bytes_with_nul(b"starnix-brk\0").unwrap())
                    .map_err(impossible_error)?;
                let length = *PAGE_SIZE as usize;
                let addr = state.map(
                    0,
                    Arc::new(vmo),
                    0,
                    length,
                    zx::VmarFlags::PERM_READ
                        | zx::VmarFlags::PERM_WRITE
                        | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
                    MappingOptions::empty(),
                    None,
                )?;
                let brk = ProgramBreak { base: addr, current: addr };
                state.brk = Some(brk);
                brk
            }
            Some(brk) => brk,
        };

        if addr < brk.base || addr > brk.base + PROGRAM_BREAK_LIMIT {
            // The requested program break is out-of-range. We're supposed to simply
            // return the current program break.
            return Ok(brk.current);
        }

        let (range, mapping) = state.mappings.get(&brk.current).ok_or(errno!(EFAULT))?;

        brk.current = addr;

        let old_end = range.end;
        let new_end = (brk.current + 1u64).round_up(*PAGE_SIZE)?;

        if new_end < old_end {
            // We've been asked to free memory.
            let delta = old_end - new_end;
            let vmo = mapping.vmo.clone();
            state.unmap(new_end, delta)?;
            let vmo_offset = new_end - brk.base;
            vmo.op_range(zx::VmoOp::DECOMMIT, vmo_offset as u64, delta as u64)
                .map_err(|e| impossible_error(e))?;
        } else if new_end > old_end {
            // We've been asked to map more memory.
            let delta = new_end - old_end;
            let vmo_offset = old_end - brk.base;
            let range = range.clone();
            let mapping = mapping.clone();

            state.mappings.remove(&range);
            match state.user_vmar.map(
                old_end - self.base_addr,
                &mapping.vmo,
                vmo_offset as u64,
                delta,
                zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::REQUIRE_NON_RESIZABLE
                    | zx::VmarFlags::SPECIFIC,
            ) {
                Ok(_) => {
                    state.mappings.insert(brk.base..new_end, mapping);
                }
                Err(e) => {
                    // We failed to extend the mapping, which means we need to add
                    // back the old mapping.
                    state.mappings.insert(brk.base..old_end, mapping);
                    return Err(Self::get_errno_for_map_err(e));
                }
            }
        }

        state.brk = Some(brk);
        return Ok(brk.current);
    }

    pub fn snapshot_to(&self, target: &MemoryManager) -> Result<(), Errno> {
        let state = self.state.read();
        let mut target_state = target.state.write();

        let mut vmos = HashMap::<zx::Koid, Arc<zx::Vmo>>::new();

        for (range, mapping) in state.mappings.iter() {
            let vmo_info = mapping.vmo.info().map_err(impossible_error)?;
            let handle_info = mapping.vmo.basic_info().map_err(impossible_error)?;
            let mut entry = vmos.entry(vmo_info.koid);
            let target_vmo = match entry {
                Entry::Vacant(v) => {
                    let vmo = if vmo_info.flags.contains(zx::VmoInfoFlags::PAGER_BACKED)
                        || mapping.options.contains(MappingOptions::SHARED)
                    {
                        mapping.vmo.clone()
                    } else {
                        let mut vmo = mapping
                            .vmo
                            .create_child(
                                zx::VmoChildOptions::SNAPSHOT | zx::VmoChildOptions::RESIZABLE,
                                0,
                                vmo_info.size_bytes,
                            )
                            .map_err(Self::get_errno_for_map_err)?;
                        // We can't use mapping.permissions for this check because it's possible
                        // that the current mapping doesn't need execute rights, but some other
                        // mapping of the same VMO does.
                        if handle_info.rights.contains(zx::Rights::EXECUTE) {
                            vmo = vmo
                                .replace_as_executable(&VMEX_RESOURCE)
                                .map_err(impossible_error)?;
                        }
                        Arc::new(vmo)
                    };
                    v.insert(vmo)
                }
                Entry::Occupied(ref mut o) => o.get_mut(),
            };
            let vmo_offset = mapping.vmo_offset + (range.start - mapping.base) as u64;
            let length = range.end - range.start;
            target_state.map(
                range.start - target.base_addr,
                target_vmo.clone(),
                vmo_offset,
                length,
                mapping.permissions | zx::VmarFlags::SPECIFIC,
                mapping.options,
                mapping.filename.clone(),
            )?;
        }

        target_state.brk = state.brk;
        target_state.executable_node = state.executable_node.clone();
        *target.dumpable.lock() = *self.dumpable.lock();

        Ok(())
    }

    pub fn exec(&self, exe_node: NamespaceNode) -> Result<(), zx::Status> {
        let mut state = self.state.write();
        let info = self.root_vmar.info()?;
        // SAFETY: This operation is safe because the VMAR is for another process.
        unsafe { state.user_vmar.destroy()? }
        state.user_vmar = create_user_vmar(&self.root_vmar, &info)?;
        state.user_vmar_info = state.user_vmar.info()?;
        state.brk = None;
        state.mappings = RangeMap::new();
        state.executable_node = Some(exe_node);

        *self.dumpable.lock() = DumpPolicy::DISABLE;
        Ok(())
    }

    pub fn executable_node(&self) -> Option<NamespaceNode> {
        self.state.read().executable_node.clone()
    }

    fn get_errno_for_map_err(status: zx::Status) -> Errno {
        match status {
            zx::Status::INVALID_ARGS => errno!(EINVAL),
            zx::Status::ACCESS_DENIED => errno!(EACCES), // or EPERM?
            zx::Status::NOT_SUPPORTED => errno!(ENODEV),
            zx::Status::NO_MEMORY => errno!(ENOMEM),
            zx::Status::OUT_OF_RANGE => errno!(ENOMEM),
            _ => impossible_error(status),
        }
    }

    pub fn map(
        &self,
        addr: UserAddress,
        vmo: Arc<zx::Vmo>,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
        options: MappingOptions,
        filename: Option<NamespaceNode>,
    ) -> Result<UserAddress, Errno> {
        let vmar_offset = if addr.is_null() { 0 } else { addr - self.base_addr };
        let mut state = self.state.write();
        state.map(vmar_offset, vmo, vmo_offset, length, flags, options, filename)
    }

    pub fn remap(
        &self,
        addr: UserAddress,
        old_length: usize,
        new_length: usize,
        flags: MremapFlags,
        new_addr: UserAddress,
    ) -> Result<UserAddress, Errno> {
        let mut state = self.state.write();
        state.remap(addr, old_length, new_length, flags, new_addr)
    }

    pub fn unmap(&self, addr: UserAddress, length: usize) -> Result<(), Errno> {
        let mut state = self.state.write();
        state.unmap(addr, length)
    }

    pub fn protect(
        &self,
        addr: UserAddress,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<(), Errno> {
        let mut state = self.state.write();
        state.protect(addr, length, flags)
    }

    pub fn set_mapping_name(
        &self,
        addr: UserAddress,
        length: usize,
        name: CString,
    ) -> Result<(), Errno> {
        let mut state = self.state.write();
        let (range, mapping) = state.mappings.get_mut(&addr).ok_or(errno!(EINVAL))?;
        if range.end - addr < length {
            return error!(EINVAL);
        }
        let _result = mapping.vmo.set_name(&name);
        mapping.name = name;
        Ok(())
    }

    /// Returns [`Ok`] if the entire range specified by `addr..(addr+length)` contains valid
    /// mappings.
    ///
    /// # Errors
    ///
    /// Returns [`Err(errno)`] where `errno` is:
    ///
    ///   - `EINVAL`: `addr` is not page-aligned, or the range is too large,
    ///   - `ENOMEM`: one or more pages in the range are not mapped.
    pub fn ensure_mapped(&self, addr: UserAddress, length: usize) -> Result<(), Errno> {
        if !addr.is_aligned(*PAGE_SIZE) {
            return error!(EINVAL);
        }

        let length = round_up_to_system_page_size(length)?;
        let end_addr = addr.checked_add(length).ok_or_else(|| errno!(EINVAL))?;
        let state = self.state.read();
        let mut last_end = addr;
        for (range, _) in state.mappings.intersection(addr..end_addr) {
            if range.start > last_end {
                // This mapping does not start immediately after the last.
                return error!(ENOMEM);
            }
            last_end = range.end;
        }
        if last_end < end_addr {
            // There is a gap of no mappings at the end of the range.
            error!(ENOMEM)
        } else {
            Ok(())
        }
    }

    #[cfg(test)]
    pub fn get_mapping_name(&self, addr: UserAddress) -> Result<CString, Errno> {
        let state = self.state.read();
        let (_, mapping) = state.mappings.get(&addr).ok_or(errno!(EFAULT))?;
        Ok(mapping.name.clone())
    }

    #[cfg(test)]
    pub fn get_mapping_count(&self) -> usize {
        let state = self.state.read();
        state.mappings.iter().count()
    }

    pub fn get_random_base(&self, length: usize) -> UserAddress {
        let state = self.state.read();
        // Allocate a vmar of the correct size, get the random location, then immediately destroy it.
        // This randomizes the load address without loading into a sub-vmar and breaking mprotect.
        // This is different from how Linux actually lays out the address space. We might need to
        // rewrite it eventually.
        let (temp_vmar, base) =
            state.user_vmar.allocate(0, length, zx::VmarFlags::empty()).unwrap();
        // SAFETY: This is safe because the vmar is not in the current process.
        unsafe { temp_vmar.destroy().unwrap() };
        UserAddress::from_ptr(base)
    }

    pub fn read_memory(&self, addr: UserAddress, bytes: &mut [u8]) -> Result<(), Errno> {
        if addr > self.state.read().max_address() {
            return error!(EFAULT);
        }
        if bytes.len() == 0 {
            return Ok(());
        }
        let actual = self.process.read_memory(addr.ptr(), bytes).map_err(|_| errno!(EFAULT))?;
        if actual != bytes.len() {
            return error!(EFAULT);
        }
        Ok(())
    }

    pub fn read_object<T: AsBytes + FromBytes>(
        &self,
        user: UserRef<T>,
        object: &mut T,
    ) -> Result<(), Errno> {
        self.read_memory(user.addr(), object.as_bytes_mut())
    }

    pub fn read_c_string<'a>(
        &self,
        string: UserCString,
        buffer: &'a mut [u8],
    ) -> Result<&'a [u8], Errno> {
        let actual = self.process.read_memory(string.ptr(), buffer).map_err(|_| errno!(EFAULT))?;
        let buffer = &mut buffer[..actual];
        let null_index = memchr::memchr(b'\0', buffer).ok_or(errno!(ENAMETOOLONG))?;
        Ok(&buffer[..null_index])
    }

    pub fn read_iovec(
        &self,
        iovec_addr: UserAddress,
        iovec_count: i32,
    ) -> Result<Vec<UserBuffer>, Errno> {
        let iovec_count: usize = iovec_count.try_into().map_err(|_| errno!(EINVAL))?;
        if iovec_count > UIO_MAXIOV as usize {
            return error!(EINVAL);
        }

        let mut data = vec![UserBuffer::default(); iovec_count];
        self.read_memory(iovec_addr, data.as_mut_slice().as_bytes_mut())?;
        Ok(data)
    }

    pub fn read_each<F>(&self, data: &[UserBuffer], mut callback: F) -> Result<(), Errno>
    where
        F: FnMut(&[u8]) -> Result<Option<()>, Errno>,
    {
        for buffer in data {
            if buffer.address.is_null() && buffer.length == 0 {
                continue;
            }
            let mut bytes = vec![0; buffer.length];
            self.read_memory(buffer.address, &mut bytes)?;
            if callback(&bytes)?.is_none() {
                break;
            }
        }
        Ok(())
    }

    pub fn read_all(&self, data: &[UserBuffer], bytes: &mut [u8]) -> Result<usize, Errno> {
        let mut offset = 0;
        for buffer in data {
            if buffer.address.is_null() && buffer.length == 0 {
                continue;
            }
            let end = std::cmp::min(offset + buffer.length, bytes.len());
            self.read_memory(buffer.address, &mut bytes[offset..end])?;
            offset = end;
            if offset == bytes.len() {
                break;
            }
        }
        Ok(offset)
    }

    pub fn write_memory(&self, addr: UserAddress, bytes: &[u8]) -> Result<(), Errno> {
        if addr > self.state.read().max_address() {
            return error!(EFAULT);
        }
        if bytes.len() == 0 {
            return Ok(());
        }
        let actual = self.process.write_memory(addr.ptr(), bytes).map_err(|_| errno!(EFAULT))?;
        if actual != bytes.len() {
            return error!(EFAULT);
        }
        Ok(())
    }

    pub fn write_object<T: AsBytes + FromBytes>(
        &self,
        user: UserRef<T>,
        object: &T,
    ) -> Result<(), Errno> {
        self.write_memory(user.addr(), &object.as_bytes())
    }

    pub fn write_each<F>(&self, data: &[UserBuffer], mut callback: F) -> Result<(), Errno>
    where
        F: FnMut(&mut [u8]) -> Result<&[u8], Errno>,
    {
        for buffer in data {
            if buffer.address.is_null() && buffer.length == 0 {
                continue;
            }
            let mut bytes = vec![0; buffer.length];
            let result = callback(&mut bytes)?;
            self.write_memory(buffer.address, &result)?;
            if result.len() != bytes.len() {
                break;
            }
        }
        Ok(())
    }

    pub fn write_all(&self, data: &[UserBuffer], bytes: &[u8]) -> Result<usize, Errno> {
        let mut offset = 0;
        for buffer in data {
            if buffer.address.is_null() && buffer.length == 0 {
                continue;
            }
            let end = std::cmp::min(offset + buffer.length, bytes.len());
            self.write_memory(buffer.address, &bytes[offset..end])?;
            offset = end;
            if offset == bytes.len() {
                break;
            }
        }
        Ok(offset)
    }
}

pub struct ProcMapsFile {
    task: Arc<Task>,
    seq: Mutex<SeqFileState<UserAddress>>,
}
impl ProcMapsFile {
    pub fn new(fs: &FileSystemHandle, task: Arc<Task>) -> FsNodeHandle {
        fs.create_node_with_ops(
            SimpleFileNode::new(move || {
                Ok(ProcMapsFile { task: Arc::clone(&task), seq: Mutex::new(SeqFileState::new()) })
            }),
            mode!(IFREG, 0o444),
        )
    }
}
impl FileOps for ProcMapsFile {
    fd_impl_seekable!();
    fd_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let state = self.task.mm.state.read();
        let mut seq = self.seq.lock();
        let mut iter = None;
        let iter = |cursor: UserAddress, sink: &mut SeqFileBuf| {
            let iter = iter.get_or_insert_with(|| state.mappings.iter_starting_at(&cursor));
            if let Some((range, map)) = iter.next() {
                let line_length = write!(
                    sink,
                    "{:08x}-{:08x} {}{}{}{} {:08x} 00:00 {} ",
                    range.start.ptr(),
                    range.end.ptr(),
                    if map.permissions.contains(zx::VmarFlags::PERM_READ) { 'r' } else { '-' },
                    if map.permissions.contains(zx::VmarFlags::PERM_WRITE) { 'w' } else { '-' },
                    if map.permissions.contains(zx::VmarFlags::PERM_EXECUTE) { 'x' } else { '-' },
                    if map.options.contains(MappingOptions::SHARED) { 's' } else { 'p' },
                    map.vmo_offset,
                    if let Some(filename) = &map.filename {
                        filename.entry.node.inode_num
                    } else {
                        0
                    }
                )?;
                if let Some(filename) = &map.filename {
                    // The filename goes at >= the 74th column (73rd when zero indexed)
                    for _ in line_length..73 {
                        sink.write(b" ");
                    }
                    sink.write(&filename.path());
                }
                sink.write(b"\n");
                return Ok(Some(range.end));
            }
            Ok(None)
        };
        seq.read_at(current_task, iter, offset, data)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(ENOSYS)
    }
}

pub struct ProcStatFile {
    task: Arc<Task>,
    seq: Mutex<SeqFileState<()>>,
}

impl ProcStatFile {
    pub fn new(fs: &FileSystemHandle, task: Arc<Task>) -> FsNodeHandle {
        fs.create_node_with_ops(
            SimpleFileNode::new(move || {
                Ok(ProcStatFile { task: Arc::clone(&task), seq: Mutex::new(SeqFileState::new()) })
            }),
            mode!(IFREG, 0o444),
        )
    }
}

impl FileOps for ProcStatFile {
    fd_impl_seekable!();
    fd_impl_nonblocking!();

    fn read_at(
        &self,
        _file: &FileObject,
        current_task: &CurrentTask,
        offset: usize,
        data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        let mut seq = self.seq.lock();
        let iter = |_cursor, sink: &mut SeqFileBuf| {
            let command = self.task.command.read();
            let command = command.as_c_str().to_str().unwrap_or("unknown");
            let mut stats = [0u64; 49];
            let mm_state = self.task.mm.state.read();
            stats[24] = mm_state.stack_base.ptr() as u64;
            let stat_str = stats.map(|n| n.to_string()).join(" ");
            write!(sink, "{} ({}) R {}\n", self.task.get_pid(), command, stat_str)?;
            Ok(None)
        };
        seq.read_at(current_task, iter, offset, data)
    }

    fn write_at(
        &self,
        _file: &FileObject,
        _current_task: &CurrentTask,
        _offset: usize,
        _data: &[UserBuffer],
    ) -> Result<usize, Errno> {
        Err(ENOSYS)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_brk() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        // Look up the given addr in the mappings table.
        let get_range = |addr: &UserAddress| {
            let state = mm.state.read();
            let (range, _) = state.mappings.get(&addr).expect("failed to find mapping");
            range.clone()
        };

        // Initialize the program break.
        let base_addr =
            mm.set_brk(UserAddress::default()).expect("failed to set initial program break");
        assert!(base_addr > UserAddress::default());

        // Check that the initial program break actually maps some memory.
        let range0 = get_range(&base_addr);
        assert_eq!(range0.start, base_addr);
        assert_eq!(range0.end, base_addr + *PAGE_SIZE);

        // Grow the program break by a tiny amount that does not actually result in a change.
        let addr1 = mm.set_brk(base_addr + 1u64).expect("failed to grow brk");
        assert_eq!(addr1, base_addr + 1u64);
        let range1 = get_range(&base_addr);
        assert_eq!(range1.start, range0.start);
        assert_eq!(range1.end, range0.end);

        // Grow the program break by a non-trival amount and observe the larger mapping.
        let addr2 = mm.set_brk(base_addr + 24893u64).expect("failed to grow brk");
        assert_eq!(addr2, base_addr + 24893u64);
        let range2 = get_range(&base_addr);
        assert_eq!(range2.start, base_addr);
        assert_eq!(range2.end, addr2.round_up(*PAGE_SIZE).unwrap());

        // Shrink the program break and observe the smaller mapping.
        let addr3 = mm.set_brk(base_addr + 14832u64).expect("failed to shrink brk");
        assert_eq!(addr3, base_addr + 14832u64);
        let range3 = get_range(&base_addr);
        assert_eq!(range3.start, base_addr);
        assert_eq!(range3.end, addr3.round_up(*PAGE_SIZE).unwrap());

        // Shrink the program break close to zero and observe the smaller mapping.
        let addr4 = mm.set_brk(base_addr + 3u64).expect("failed to drastically shrink brk");
        assert_eq!(addr4, base_addr + 3u64);
        let range4 = get_range(&base_addr);
        assert_eq!(range4.start, base_addr);
        assert_eq!(range4.end, addr4.round_up(*PAGE_SIZE).unwrap());

        // Shrink the program break close to zero and observe that the mapping is not entirely gone.
        let addr5 = mm.set_brk(base_addr).expect("failed to drastically shrink brk to zero");
        assert_eq!(addr5, base_addr);
        let range5 = get_range(&base_addr);
        assert_eq!(range5.start, base_addr);
        assert_eq!(range5.end, addr5 + *PAGE_SIZE);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_mm_exec() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let has = |addr: &UserAddress| -> bool {
            let state = mm.state.read();
            state.mappings.get(addr).is_some()
        };

        let brk_addr =
            mm.set_brk(UserAddress::default()).expect("failed to set initial program break");
        assert!(brk_addr > UserAddress::default());
        assert!(has(&brk_addr));

        let mapped_addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE);
        assert!(mapped_addr > UserAddress::default());
        assert!(has(&mapped_addr));

        let node = current_task.lookup_path_from_root(b"/").unwrap();
        mm.exec(node).expect("failed to exec memory manager");

        assert!(!has(&brk_addr));
        assert!(!has(&mapped_addr));

        // Check that the old addresses are actually available for mapping.
        let brk_addr2 = map_memory(&current_task, brk_addr, *PAGE_SIZE);
        assert_eq!(brk_addr, brk_addr2);
        let mapped_addr2 = map_memory(&current_task, mapped_addr, *PAGE_SIZE);
        assert_eq!(mapped_addr, mapped_addr2);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_each() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let page_size = *PAGE_SIZE;
        let addr = map_memory(&current_task, UserAddress::default(), 64 * page_size);

        let data: Vec<u8> = (0..1024).map(|i| (i % 256) as u8).collect();
        mm.write_memory(addr, &data).expect("failed to write test data");

        let iovec = vec![
            UserBuffer { address: addr, length: 25 },
            UserBuffer { address: addr + 64usize, length: 12 },
        ];

        let mut read_count = 0;
        mm.read_each(&iovec, |buffer| {
            match read_count {
                0 => {
                    assert_eq!(&data[..25], buffer);
                }
                1 => {
                    assert_eq!(&data[64..76], buffer);
                }
                _ => {
                    assert!(false);
                }
            };
            read_count += 1;
            Ok(Some(()))
        })
        .expect("failed to read each");
        assert_eq!(read_count, 2);

        read_count = 0;
        mm.read_each(&iovec, |_| {
            read_count += 1;
            Ok(None)
        })
        .expect("failed to read each");
        assert_eq!(read_count, 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_all() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let page_size = *PAGE_SIZE;
        let addr = map_memory(&current_task, UserAddress::default(), 64 * page_size);

        let data: Vec<u8> = (0..1024).map(|i| (i % 256) as u8).collect();
        mm.write_memory(addr, &data).expect("failed to write test data");

        let iovec = vec![
            UserBuffer { address: addr, length: 25 },
            UserBuffer { address: addr + 64usize, length: 12 },
        ];

        let mut buffer = vec![0u8; 37];
        mm.read_all(&iovec, &mut buffer).expect("failed to read all");
        assert_eq!(&data[..25], &buffer[..25]);
        assert_eq!(&data[64..76], &buffer[25..]);

        buffer = vec![0u8; 27];
        mm.read_all(&iovec, &mut buffer).expect("failed to read all");
        assert_eq!(&data[..25], &buffer[..25]);
        assert_eq!(&data[64..66], &buffer[25..27]);

        buffer = vec![0u8; 42];
        assert_eq!(Ok(37), mm.read_all(&iovec, &mut buffer));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_each() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let page_size = *PAGE_SIZE;
        let addr = map_memory(&current_task, UserAddress::default(), 64 * page_size);

        let data: Vec<u8> = (0..1024).map(|i| (i % 256) as u8).collect();

        let iovec = vec![
            UserBuffer { address: addr, length: 25 },
            UserBuffer { address: addr + 64usize, length: 12 },
        ];

        let mut write_count = 0;
        mm.write_each(&iovec, |buffer| {
            match write_count {
                0 => {
                    assert_eq!(buffer.len(), 25);
                    buffer.copy_from_slice(&data[..25]);
                }
                1 => {
                    assert_eq!(buffer.len(), 12);
                    buffer.copy_from_slice(&data[25..37]);
                }
                _ => {
                    assert!(false);
                }
            };
            write_count += 1;
            Ok(buffer)
        })
        .expect("failed to write each");
        assert_eq!(write_count, 2);

        let mut written = vec![0u8; 1024];
        mm.read_memory(addr, &mut written).expect("failed to read back memory");
        assert_eq!(&written[..25], &data[..25]);
        assert_eq!(&written[64..76], &data[25..37]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_write_all() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let page_size = *PAGE_SIZE;
        let addr = map_memory(&current_task, UserAddress::default(), 64 * page_size);

        let data: Vec<u8> = (0..1024).map(|i| (i % 256) as u8).collect();

        let iovec = vec![
            UserBuffer { address: addr, length: 25 },
            UserBuffer { address: addr + 64usize, length: 12 },
        ];

        mm.write_all(&iovec, &data[..37]).expect("failed to write all");

        let mut written = vec![0u8; 1024];
        mm.read_memory(addr, &mut written).expect("failed to read back memory");

        assert_eq!(&written[..25], &data[..25]);
        assert_eq!(&written[64..76], &data[25..37]);

        written = vec![0u8; 1024];
        mm.write_memory(addr, &mut written).expect("clear memory");
        mm.write_all(&iovec, &data[..27]).expect("failed to write all");
        mm.read_memory(addr, &mut written).expect("failed to read back memory");
        assert_eq!(&written[..25], &data[..25]);
        assert_eq!(&written[64..66], &data[25..27]);

        assert_eq!(Ok(37), mm.write_all(&iovec, &data[..42]));
    }

    /// Maps two pages, then unmaps the first page.
    /// The second page should be re-mapped with a new child COW VMO.
    #[fasync::run_singlethreaded(test)]
    async fn test_unmap_beginning() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);

        let original_vmo = {
            let state = mm.state.read();
            let (range, mapping) = state.mappings.get(&addr).expect("mapping");
            assert_eq!(range.start, addr);
            assert_eq!(range.end, addr + (*PAGE_SIZE * 2));
            assert_eq!(mapping.base, addr);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE * 2);
            mapping.vmo.clone()
        };

        assert_eq!(mm.unmap(addr, *PAGE_SIZE as usize), Ok(()));

        {
            let state = mm.state.read();

            // The first page should be unmapped.
            assert!(state.mappings.get(&addr).is_none());

            // The second page should be a new child COW VMO.
            let (range, mapping) = state.mappings.get(&(addr + *PAGE_SIZE)).expect("second page");
            assert_eq!(range.start, addr + *PAGE_SIZE);
            assert_eq!(range.end, addr + *PAGE_SIZE * 2);
            assert_eq!(mapping.base, addr + *PAGE_SIZE);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE);
            assert_ne!(original_vmo.get_koid().unwrap(), mapping.vmo.get_koid().unwrap());
        }
    }

    /// Maps two pages, then unmaps the second page.
    /// The first page's VMO should be shrunk.
    #[fasync::run_singlethreaded(test)]
    async fn test_unmap_end() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 2);

        let original_vmo = {
            let state = mm.state.read();
            let (range, mapping) = state.mappings.get(&addr).expect("mapping");
            assert_eq!(range.start, addr);
            assert_eq!(range.end, addr + (*PAGE_SIZE * 2));
            assert_eq!(mapping.base, addr);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE * 2);
            mapping.vmo.clone()
        };

        assert_eq!(mm.unmap(addr + *PAGE_SIZE, *PAGE_SIZE as usize), Ok(()));

        {
            let state = mm.state.read();

            // The second page should be unmapped.
            assert!(state.mappings.get(&(addr + *PAGE_SIZE)).is_none());

            // The first page's VMO should be the same as the original, only shrunk.
            let (range, mapping) = state.mappings.get(&addr).expect("first page");
            assert_eq!(range.start, addr);
            assert_eq!(range.end, addr + *PAGE_SIZE);
            assert_eq!(mapping.base, addr);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE);
            assert_eq!(original_vmo.get_koid().unwrap(), mapping.vmo.get_koid().unwrap());
        }
    }

    /// Maps three pages, then unmaps the middle page.
    /// The last page should be re-mapped with a new COW child VMO.
    /// The first page's VMO should be shrunk,
    #[fasync::run_singlethreaded(test)]
    async fn test_unmap_middle() {
        let (_kernel, current_task) = create_kernel_and_task();
        let mm = &current_task.mm;

        let addr = map_memory(&current_task, UserAddress::default(), *PAGE_SIZE * 3);

        let original_vmo = {
            let state = mm.state.read();
            let (range, mapping) = state.mappings.get(&addr).expect("mapping");
            assert_eq!(range.start, addr);
            assert_eq!(range.end, addr + (*PAGE_SIZE * 3));
            assert_eq!(mapping.base, addr);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE * 3);
            mapping.vmo.clone()
        };

        assert_eq!(mm.unmap(addr + *PAGE_SIZE, *PAGE_SIZE as usize), Ok(()));

        {
            let state = mm.state.read();

            // The middle page should be unmapped.
            assert!(state.mappings.get(&(addr + *PAGE_SIZE)).is_none());

            // The first page's VMO should be the same as the original, only shrunk.
            let (range, mapping) = state.mappings.get(&addr).expect("first page");
            assert_eq!(range.start, addr);
            assert_eq!(range.end, addr + *PAGE_SIZE);
            assert_eq!(mapping.base, addr);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE);
            assert_eq!(original_vmo.get_koid().unwrap(), mapping.vmo.get_koid().unwrap());

            // The last page should be a new child COW VMO.
            let (range, mapping) = state.mappings.get(&(addr + *PAGE_SIZE * 2)).expect("last page");
            assert_eq!(range.start, addr + *PAGE_SIZE * 2);
            assert_eq!(range.end, addr + *PAGE_SIZE * 3);
            assert_eq!(mapping.base, addr + *PAGE_SIZE * 2);
            assert_eq!(mapping.vmo_offset, 0);
            assert_eq!(mapping.vmo.get_size().unwrap(), *PAGE_SIZE);
            assert_ne!(original_vmo.get_koid().unwrap(), mapping.vmo.get_koid().unwrap());
        }
    }
}
