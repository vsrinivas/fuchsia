// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_zircon::{self as zx, HandleBased, Status};
use lazy_static::lazy_static;
use parking_lot::RwLock;
use zerocopy::{AsBytes, FromBytes};

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

const PROGRAM_BREAK_LIMIT: u64 = 64 * 1024 * 1024;

pub struct MemoryManager {
    /// A handle to the underlying Zircon process object.
    // TODO: Remove this handle once we can read and write process memory directly.
    pub process: zx::Process,

    pub root_vmar: zx::Vmar,

    program_break: RwLock<ProgramBreak>,
}

impl MemoryManager {
    pub fn new(process: zx::Process, root_vmar: zx::Vmar) -> Self {
        MemoryManager { process, root_vmar, program_break: RwLock::new(ProgramBreak::default()) }
    }

    pub fn set_program_break(&self, addr: UserAddress) -> Result<UserAddress, Status> {
        let mut program_break = self.program_break.write();
        if program_break.vmar.is_invalid_handle() {
            // TODO: This allocation places the program break at a random location in the
            // child's address space. However, we're supposed to put this memory directly
            // above the last segment of the ELF for the child.
            let (vmar, raw_addr) = self.root_vmar.allocate(
                0,
                PROGRAM_BREAK_LIMIT as usize,
                zx::VmarFlags::CAN_MAP_SPECIFIC
                    | zx::VmarFlags::CAN_MAP_READ
                    | zx::VmarFlags::CAN_MAP_WRITE,
            )?;
            let vmo = zx::Vmo::create(PROGRAM_BREAK_LIMIT)?;
            program_break.vmar = vmar;
            program_break.vmo = vmo;
            program_break.base = UserAddress::from(raw_addr);
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
