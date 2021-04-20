// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{self as zx, sys::zx_thread_state_general_regs_t, HandleBased, Status},
    parking_lot::{Mutex, RwLock},
    std::sync::Arc,
};

use crate::uapi::*;

pub struct ProgramBreak {
    vmar: zx::Vmar,
    vmo: zx::Vmo,

    // These base address at which the data segment is mapped.
    base: UserAddress,

    // The current program break.
    //
    // The addresses from [base, current.round_up(PAGE_SIZE)) are mapped into the
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
const PAGE_SIZE: u64 = 4 * 1024;

pub struct MemoryManager {
    pub root_vmar: zx::Vmar,
    program_break: RwLock<ProgramBreak>,
}

impl MemoryManager {
    pub fn new(root_vmar: zx::Vmar) -> Self {
        MemoryManager { root_vmar, program_break: RwLock::new(ProgramBreak::default()) }
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
            let aligned_previous = program_break.current.round_up(PAGE_SIZE);
            program_break.current = addr;
            let aligned_current = program_break.current.round_up(PAGE_SIZE);

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
        let aligned_previous = program_break.current.round_up(PAGE_SIZE);
        program_break.current = addr;
        let aligned_current = program_break.current.round_up(PAGE_SIZE);

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
}

#[derive(Default)]
pub struct SecurityContext {
    pub uid: uid_t,
    pub gid: uid_t,
    pub euid: uid_t,
    pub egid: uid_t,
}

pub struct ProcessContext {
    pub process_id: pid_t,
    pub handle: zx::Process,
    pub security: SecurityContext,
    pub mm: MemoryManager,

    /// The exit code reported by the process, if the process has exited.
    pub exit_code: Mutex<Option<i32>>,
}

impl ProcessContext {
    pub fn read_memory(&self, addr: UserAddress, bytes: &mut [u8]) -> Result<(), Errno> {
        let actual = self.handle.read_memory(addr.ptr(), bytes).map_err(|_| EFAULT)?;
        if actual != bytes.len() {
            return Err(EFAULT);
        }
        Ok(())
    }

    pub fn write_memory(&self, addr: UserAddress, bytes: &[u8]) -> Result<(), Errno> {
        let actual = self.handle.write_memory(addr.ptr(), bytes).map_err(|_| EFAULT)?;
        if actual != bytes.len() {
            return Err(EFAULT);
        }
        Ok(())
    }
}

pub struct ThreadContext {
    pub thread_id: pid_t,

    /// A handle to the underlying Zircon thread object.
    pub handle: zx::Thread,

    /// A reference to the process the thread is running in. The process context is wrapped in an
    /// `Arc` since multiple threads share the same process context.
    pub process: Arc<ProcessContext>,

    /// A copy of the registers associated with the Zircon thread. Up-to-date values can be read
    /// from `self.handle.read_state_general_regs()`. To write these values back to the thread, call
    /// `self.handle.write_state_general_regs(self.registers)`.
    pub registers: zx_thread_state_general_regs_t,
    pub set_child_tid: Mutex<UserAddress>,
    pub clear_child_tid: Mutex<UserAddress>,
}

impl ThreadContext {
    pub fn new(handle: zx::Thread, process: Arc<ProcessContext>) -> ThreadContext {
        ThreadContext {
            thread_id: 7, // TODO: Assign from a thread map.
            handle: handle,
            process: process,
            registers: zx_thread_state_general_regs_t::default(),
            set_child_tid: Mutex::new(UserAddress::default()),
            clear_child_tid: Mutex::new(UserAddress::default()),
        }
    }
}
