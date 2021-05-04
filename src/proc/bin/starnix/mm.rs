// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, AsHandleRef, HandleBased, Status};
use lazy_static::lazy_static;
use parking_lot::RwLock;
use std::ffi::CString;
use std::sync::Arc;
use zerocopy::{AsBytes, FromBytes};

use crate::collections::*;
use crate::logging::*;
use crate::uapi::*;

lazy_static! {
    pub static ref PAGE_SIZE: u64 = zx::system_get_page_size() as u64;
}

pub struct ProgramBreak {
    vmar: zx::Vmar,
    vmo: zx::Vmo,

    // These base address at which the data segment is mapped.
    base: UserAddress,

    // The current program break.
    //
    // The addresses from [base, current.round_up(*PAGE_SIZE)) are mapped into the
    // client address space from the underlying |vmo|.
    current: UserAddress,
}

impl Default for ProgramBreak {
    fn default() -> ProgramBreak {
        return ProgramBreak {
            vmar: zx::Handle::invalid().into(),
            vmo: zx::Handle::invalid().into(),
            base: UserAddress::default(),
            current: UserAddress::default(),
        };
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

    /// A name associated with the mapping. Set by prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME, ...).
    name: CString,
}

impl Mapping {
    fn new(
        base: UserAddress,
        vmo: zx::Vmo,
        vmo_offset: u64,
        flags: zx::VmarFlags,
    ) -> Mapping {
        Mapping {
            base,
            vmo: Arc::new(vmo),
            vmo_offset,
            permissions: flags
                & (zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::PERM_EXECUTE),
            name: CString::default(),
        }
    }
}

const PROGRAM_BREAK_LIMIT: u64 = 64 * 1024 * 1024;

pub struct MemoryManager {
    /// A handle to the underlying Zircon process object.
    // TODO: Remove this handle once we can read and write process memory directly.
    process: zx::Process,

    /// The VMAR managed by this memory manager.
    ///
    /// Use the map / unmap functions on the MemoryManager rather than directly
    /// mapping/unmapping memory from the VMAR so that the mappings can be tracked
    /// by the MemoryManager.
    // TODO: Make root_vmar private to force clients through the MemoryManager
    // interface.
    pub root_vmar: zx::Vmar,

    /// State for the brk and sbrk syscalls.
    program_break: RwLock<ProgramBreak>,

    /// The memory mappings currently used by this address space.
    ///
    /// The mappings record which VMO backs each address.
    mappings: RwLock<RangeMap<UserAddress, Mapping>>,
}

impl MemoryManager {
    pub fn new(process: zx::Process, root_vmar: zx::Vmar) -> Self {
        MemoryManager {
            process,
            root_vmar,
            program_break: RwLock::new(ProgramBreak::default()),
            mappings: RwLock::new(RangeMap::<UserAddress, Mapping>::new()),
        }
    }

    pub fn set_program_break(&self, addr: UserAddress) -> Result<UserAddress, Status> {
        let mut program_break = self.program_break.write();
        if program_break.vmar.is_invalid_handle() {
            // TODO: This allocation places the program break at a random location in the
            // child's address space. However, we're supposed to put this memory directly
            // above the last segment of the ELF for the child.
            let (vmar, ptr) = self.root_vmar.allocate(
                0,
                PROGRAM_BREAK_LIMIT as usize,
                zx::VmarFlags::CAN_MAP_SPECIFIC
                    | zx::VmarFlags::CAN_MAP_READ
                    | zx::VmarFlags::CAN_MAP_WRITE,
            )?;
            let vmo = zx::Vmo::create(PROGRAM_BREAK_LIMIT)?;
            program_break.vmar = vmar;
            program_break.vmo = vmo;
            program_break.base = UserAddress::from_ptr(ptr);
            program_break.current = program_break.base;
        }
        if addr < program_break.base || addr > program_break.base + PROGRAM_BREAK_LIMIT {
            // The requested program break is out-of-range. We're supposed to simply
            // return the current program break.
            return Ok(program_break.current);
        }
        if addr < program_break.current {
            // The client wishes to free up memory. Adjust the program break to the new
            // location.
            let aligned_previous = program_break.current.round_up(*PAGE_SIZE);
            program_break.current = addr;
            let aligned_current = program_break.current.round_up(*PAGE_SIZE);

            let len = aligned_current - aligned_previous;
            if len > 0 {
                // If we crossed a page boundary, we can actually unmap and free up the
                // unused pages.
                let offset = aligned_previous - program_break.base;
                unsafe { program_break.vmar.unmap(aligned_current.ptr(), len)? };
                program_break.vmo.op_range(zx::VmoOp::DECOMMIT, offset as u64, len as u64)?;
            }
            return Ok(program_break.current);
        }

        // Otherwise, we've been asked to increase the page break.
        let aligned_previous = program_break.current.round_up(*PAGE_SIZE);
        program_break.current = addr;
        let aligned_current = program_break.current.round_up(*PAGE_SIZE);

        let len = aligned_current - aligned_previous;
        if len > 0 {
            // If we crossed a page boundary, we need to map more of the underlying VMO
            // into the client's address space.
            let offset = aligned_previous - program_break.base;
            program_break.vmar.map(
                offset,
                &program_break.vmo,
                offset as u64,
                len,
                zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::REQUIRE_NON_RESIZABLE
                    | zx::VmarFlags::SPECIFIC,
            )?;
        }
        return Ok(program_break.current);
    }

    pub fn map(
        &self,
        addr: UserAddress,
        vmo: zx::Vmo,
        vmo_offset: u64,
        length: usize,
        flags: zx::VmarFlags,
    ) -> Result<UserAddress, Errno> {
        let root_base = self.root_vmar.info().unwrap().base;
        let vmar_offset = if addr.ptr() == 0 { 0 } else { addr.ptr() - root_base };
        let mut mappings = self.mappings.write();
        let addr = UserAddress::from_ptr(
            self.root_vmar.map(vmar_offset, &vmo, vmo_offset, length, flags).map_err(|s| {
                match s {
                    zx::Status::INVALID_ARGS => EINVAL,
                    zx::Status::ACCESS_DENIED => EACCES, // or EPERM?
                    zx::Status::NOT_SUPPORTED => ENODEV,
                    zx::Status::NO_MEMORY => ENOMEM,
                    _ => impossible_error(s),
                }
            })?,
        );
        let mapping = Mapping::new(addr, vmo, vmo_offset, flags);
        let end = (addr + length).round_up(*PAGE_SIZE);
        mappings.insert(addr..end, mapping);
        Ok(addr)
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

    pub fn read_memory(&self, addr: UserAddress, bytes: &mut [u8]) -> Result<(), Errno> {
        let actual = self.process.read_memory(addr.ptr(), bytes).map_err(|_| EFAULT)?;
        if actual != bytes.len() {
            return Err(EFAULT);
        }
        Ok(())
    }

    #[allow(dead_code)] // Not used yet.
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
