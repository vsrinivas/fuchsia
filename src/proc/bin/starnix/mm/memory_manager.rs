// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased};
use lazy_static::lazy_static;
use parking_lot::{Mutex, RwLock, RwLockUpgradableReadGuard};
use process_builder::elf_load;
use std::ffi::{CStr, CString};
use std::mem;
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

#[cfg(test)]
use std::collections::HashMap;

use crate::collections::*;
use crate::logging::*;
use crate::types::*;

lazy_static! {
    pub static ref PAGE_SIZE: u64 = zx::system_get_page_size() as u64;
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

    /// A name associated with the mapping. Set by prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ...).
    name: CString,
}

impl Mapping {
    fn new(base: UserAddress, vmo: Arc<zx::Vmo>, vmo_offset: u64, flags: zx::VmarFlags) -> Mapping {
        Mapping {
            base,
            vmo,
            vmo_offset,
            permissions: flags
                & (zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::PERM_EXECUTE),
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

pub struct MemoryManager {
    /// A handle to the underlying Zircon process object.
    // TODO: Remove this handle once we can read and write process memory directly.
    process: zx::Process,

    /// The VMAR managed by this memory manager.
    ///
    /// Use the map / unmap functions on the MemoryManager rather than directly
    /// mapping/unmapping memory from the VMAR so that the mappings can be tracked
    /// by the MemoryManager.
    root_vmar: zx::Vmar,

    /// The base address of the root_vmar.
    pub vmar_base: UserAddress,

    /// State for the brk and sbrk syscalls.
    program_break: Mutex<Option<ProgramBreak>>,

    /// The memory mappings currently used by this address space.
    ///
    /// The mappings record which VMO backs each address.
    mappings: RwLock<RangeMap<UserAddress, Mapping>>,

    /// Whether this address space is dumpable.
    pub dumpable: Mutex<DumpPolicy>,
}

impl MemoryManager {
    pub fn new(process: zx::Process, root_vmar: zx::Vmar) -> Self {
        let vmar_base = UserAddress::from_ptr(root_vmar.info().unwrap().base);
        MemoryManager {
            process,
            root_vmar,
            vmar_base,
            program_break: Mutex::new(None),
            mappings: RwLock::new(RangeMap::<UserAddress, Mapping>::new()),
            dumpable: Mutex::new(DumpPolicy::DISABLE),
        }
    }

    pub fn set_brk(&self, addr: UserAddress) -> Result<UserAddress, Errno> {
        let mut program_break = self.program_break.lock();

        // Ensure that a program break exists by mapping at least one page.
        let mut brk = match *program_break {
            None => {
                let vmo = zx::Vmo::create(PROGRAM_BREAK_LIMIT).map_err(|_| ENOMEM)?;
                vmo.set_name(CStr::from_bytes_with_nul(b"starnix-brk\0").unwrap())
                    .map_err(impossible_error)?;
                let length = *PAGE_SIZE as usize;
                let addr = self.map(
                    UserAddress::default(),
                    vmo,
                    0,
                    length,
                    zx::VmarFlags::PERM_READ
                        | zx::VmarFlags::PERM_WRITE
                        | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
                )?;
                let brk = ProgramBreak { base: addr, current: addr };
                *program_break = Some(brk);
                brk
            }
            Some(brk) => brk,
        };

        if addr < brk.base || addr > brk.base + PROGRAM_BREAK_LIMIT {
            // The requested program break is out-of-range. We're supposed to simply
            // return the current program break.
            return Ok(brk.current);
        }

        let mappings = self.mappings.upgradable_read();
        let (range, mapping) = mappings.get(&brk.current).ok_or(EFAULT)?;

        brk.current = addr;

        let old_end = range.end;
        let new_end = (brk.current + 1u64).round_up(*PAGE_SIZE);

        if new_end < old_end {
            // We've been asked to free memory.
            let delta = old_end - new_end;
            let vmo = mapping.vmo.clone();
            mem::drop(mappings);
            self.unmap(new_end, delta)?;
            let vmo_offset = new_end - brk.base;
            vmo.op_range(zx::VmoOp::DECOMMIT, vmo_offset as u64, delta as u64)
                .map_err(|e| impossible_error(e))?;
        } else if new_end > old_end {
            // We've been asked to map more memory.
            let delta = new_end - old_end;
            let vmo_offset = old_end - brk.base;
            let range = range.clone();
            let mapping = mapping.clone();

            let mut mappings = RwLockUpgradableReadGuard::upgrade(mappings);
            mappings.remove(&range);
            match self.root_vmar.map(
                old_end - self.vmar_base,
                &mapping.vmo,
                vmo_offset as u64,
                delta,
                zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::REQUIRE_NON_RESIZABLE
                    | zx::VmarFlags::SPECIFIC,
            ) {
                Ok(_) => {
                    mappings.insert(brk.base..new_end, mapping);
                }
                Err(e) => {
                    // We failed to extend the mapping, which means we need to add
                    // back the old mapping.
                    mappings.insert(brk.base..old_end, mapping);
                    return Err(Self::get_errno_for_map_err(e));
                }
            }
        }

        *program_break = Some(brk);
        return Ok(brk.current);
    }

    #[cfg(test)]
    pub fn snapshot_to(&self, target: &MemoryManager) -> Result<(), Errno> {
        // TODO: Snapshot program_break.
        // TODO: Snapshot dumpable.

        let mappings = self.mappings.read();
        let mut vmos = HashMap::<zx::Koid, Result<Arc<zx::Vmo>, zx::Status>>::new();

        for (range, mapping) in mappings.iter() {
            let vmo_info = mapping.vmo.info().map_err(impossible_error)?;
            let entry = vmos.entry(vmo_info.koid).or_insert_with(|| {
                if vmo_info.flags.contains(zx::VmoInfoFlags::PAGER_BACKED) {
                    Ok(mapping.vmo.clone())
                } else {
                    mapping
                        .vmo
                        .create_child(zx::VmoChildOptions::SNAPSHOT, 0, vmo_info.size_bytes)
                        .map(|vmo| Arc::new(vmo))
                }
            });
            match entry {
                Ok(target_vmo) => {
                    let vmo_offset = mapping.vmo_offset + (range.start - mapping.base) as u64;
                    let length = range.end - range.start;
                    target
                        .map_internal(
                            range.start - target.vmar_base,
                            target_vmo.clone(),
                            vmo_offset,
                            length,
                            mapping.permissions | zx::VmarFlags::SPECIFIC,
                        )
                        .map_err(Self::get_errno_for_map_err)?;
                }
                Err(_) => {
                    return Err(ENOMEM);
                }
            };
        }

        Ok(())
    }

    fn map_internal(
        &self,
        vmar_offset: usize,
        vmo: Arc<zx::Vmo>,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<UserAddress, zx::Status> {
        let mut mappings = self.mappings.write();
        let addr = UserAddress::from_ptr(self.root_vmar.map(
            vmar_offset,
            &vmo,
            vmo_offset,
            length,
            flags,
        )?);
        let mapping = Mapping::new(addr, vmo, vmo_offset, flags);
        let end = (addr + length).round_up(*PAGE_SIZE);
        mappings.insert(addr..end, mapping);
        Ok(addr)
    }

    fn get_errno_for_map_err(status: zx::Status) -> Errno {
        match status {
            zx::Status::INVALID_ARGS => EINVAL,
            zx::Status::ACCESS_DENIED => EACCES, // or EPERM?
            zx::Status::NOT_SUPPORTED => ENODEV,
            zx::Status::NO_MEMORY => ENOMEM,
            _ => impossible_error(status),
        }
    }

    pub fn map(
        &self,
        addr: UserAddress,
        vmo: zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<UserAddress, Errno> {
        let vmar_offset = if addr.is_null() { 0 } else { addr - self.vmar_base };
        self.map_internal(vmar_offset, Arc::new(vmo), vmo_offset, length, flags)
            .map_err(Self::get_errno_for_map_err)
    }

    pub fn unmap(&self, addr: UserAddress, length: usize) -> Result<(), Errno> {
        let mut mappings = self.mappings.write();
        // This operation is safe because we're operating on another process.
        match unsafe { self.root_vmar.unmap(addr.ptr(), length) } {
            Ok(_) => Ok(()),
            Err(zx::Status::NOT_FOUND) => Ok(()),
            Err(zx::Status::INVALID_ARGS) => Err(EINVAL),
            Err(status) => Err(impossible_error(status)),
        }?;
        let end = (addr + length).round_up(*PAGE_SIZE);
        mappings.remove(&(addr..end));
        Ok(())
    }

    pub fn protect(
        &self,
        addr: UserAddress,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<(), Errno> {
        let mut mappings = self.mappings.write();
        let (_, mapping) = mappings.get(&addr).ok_or(EINVAL)?;
        let mapping = mapping.with_flags(flags);

        // SAFETY: This is safe because the vmar belongs to a different process.
        unsafe { self.root_vmar.protect(addr.ptr(), length, flags) }.map_err(|s| match s {
            zx::Status::INVALID_ARGS => EINVAL,
            // TODO: This should still succeed and change protection on whatever is mapped.
            zx::Status::NOT_FOUND => EINVAL,
            zx::Status::ACCESS_DENIED => EACCES,
            _ => impossible_error(s),
        })?;

        let end = (addr + length).round_up(*PAGE_SIZE);
        mappings.insert(addr..end, mapping);
        Ok(())
    }

    pub fn set_mapping_name(
        &self,
        addr: UserAddress,
        length: usize,
        name: CString,
    ) -> Result<(), Errno> {
        let mut mappings = self.mappings.write();
        let (range, mapping) = mappings.get_mut(&addr).ok_or(EINVAL)?;
        if range.end - addr < length {
            return Err(EINVAL);
        }
        let _result = mapping.vmo.set_name(&name);
        mapping.name = name;
        Ok(())
    }

    #[cfg(test)]
    pub fn get_mapping_name(&self, addr: UserAddress) -> Result<CString, Errno> {
        let mapping = self.mappings.read();
        let (_, mapping) = mapping.get(&addr).ok_or(EFAULT)?;
        Ok(mapping.name.clone())
    }

    #[cfg(test)]
    pub fn get_mapping_count(&self) -> usize {
        let mapping = self.mappings.read();
        mapping.iter().count()
    }

    pub fn get_random_base(&self, length: usize) -> UserAddress {
        // Allocate a vmar of the correct size, get the random location, then immediately destroy it.
        // This randomizes the load address without loading into a sub-vmar and breaking mprotect.
        // This is different from how Linux actually lays out the address space. We might need to
        // rewrite it eventually.
        let (temp_vmar, base) = self.root_vmar.allocate(0, length, zx::VmarFlags::empty()).unwrap();
        // SAFETY: This is safe because the vmar is not in the current process.
        unsafe { temp_vmar.destroy().unwrap() };
        UserAddress::from_ptr(base)
    }

    pub fn read_memory(&self, addr: UserAddress, bytes: &mut [u8]) -> Result<(), Errno> {
        let actual = self.process.read_memory(addr.ptr(), bytes).map_err(|_| EFAULT)?;
        if actual != bytes.len() {
            return Err(EFAULT);
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
        let actual = self.process.read_memory(string.ptr(), buffer).map_err(|_| EFAULT)?;
        let buffer = &mut buffer[..actual];
        let null_index = memchr::memchr(b'\0', buffer).ok_or(ENAMETOOLONG)?;
        Ok(&buffer[..null_index])
    }

    pub fn write_memory(&self, addr: UserAddress, bytes: &[u8]) -> Result<(), Errno> {
        let actual = self.process.write_memory(addr.ptr(), bytes).map_err(|_| EFAULT)?;
        if actual != bytes.len() {
            return Err(EFAULT);
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
}

impl elf_load::Mapper for MemoryManager {
    fn map(
        &self,
        vmar_offset: usize,
        vmo: &zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<usize, zx::Status> {
        let vmo = Arc::new(vmo.duplicate_handle(zx::Rights::SAME_RIGHTS)?);
        self.map_internal(vmar_offset, vmo, vmo_offset, length, flags).map(|addr| addr.ptr())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;

    use crate::testing::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_brk() {
        let (_kernel, task_owner) = create_kernel_and_task();
        let mm = &task_owner.task.mm;

        // Look up the given addr in the mappings table.
        let get_range = |addr: &UserAddress| {
            let mappings = mm.mappings.read();
            let (range, _) = mappings.get(&addr).expect("failed to find mapping");
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
        assert_eq!(range2.end, addr2.round_up(*PAGE_SIZE));

        // Shrink the program break and observe the smaller mapping.
        let addr3 = mm.set_brk(base_addr + 14832u64).expect("failed to shrink brk");
        assert_eq!(addr3, base_addr + 14832u64);
        let range3 = get_range(&base_addr);
        assert_eq!(range3.start, base_addr);
        assert_eq!(range3.end, addr3.round_up(*PAGE_SIZE));

        // Shrink the program break close to zero and observe the smaller mapping.
        let addr4 = mm.set_brk(base_addr + 3u64).expect("failed to drastically shrink brk");
        assert_eq!(addr4, base_addr + 3u64);
        let range4 = get_range(&base_addr);
        assert_eq!(range4.start, base_addr);
        assert_eq!(range4.end, addr4.round_up(*PAGE_SIZE));

        // Shrink the program break close to zero and observe that the mapping is not entirely gone.
        let addr5 = mm.set_brk(base_addr).expect("failed to drastically shrink brk to zero");
        assert_eq!(addr5, base_addr);
        let range5 = get_range(&base_addr);
        assert_eq!(range5.start, base_addr);
        assert_eq!(range5.end, addr5 + *PAGE_SIZE);
    }
}
