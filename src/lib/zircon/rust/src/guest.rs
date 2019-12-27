// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Port, Status};
use fuchsia_zircon_sys as sys;

/// Wrapper type for guest physical addresses.
#[repr(transparent)]
pub struct GPAddr(pub usize);

/// An object representing a Zircon guest
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Guest(Handle);
impl_handle_based!(Guest);

impl Guest {
    // TODO: create (aka zx_guest_create) is not defined here as resources do not yet have a type
    // safe definition.

    /// Set a bell trap for the given guest physical address range that will be delivered on the specified `Port`.
    pub fn set_trap_bell(
        &self,
        addr: GPAddr,
        size: usize,
        port: &Port,
        key: u64,
    ) -> Result<(), Status> {
        ok(unsafe {
            sys::zx_guest_set_trap(
                self.raw_handle(),
                sys::ZX_GUEST_TRAP_BELL,
                addr.0,
                size,
                port.raw_handle(),
                key,
            )
        })
    }

    /// Set a memory trap for the given guest physical address range.
    ///
    /// The trap will be delivered through calls to `Resume` on the guests `Vcpu`.
    pub fn set_mem_trap(&self, addr: GPAddr, size: usize, key: u64) -> Result<(), Status> {
        ok(unsafe {
            sys::zx_guest_set_trap(
                self.raw_handle(),
                sys::ZX_GUEST_TRAP_MEM,
                addr.0,
                size,
                sys::ZX_HANDLE_INVALID,
                key,
            )
        })
    }

    /// Set an IO trap for the given port range in the guest.
    ///
    /// The trap will be delivered through calls to `Resume` on the guests `Vcpu`.
    pub fn set_io_trap(&self, addr: u16, size: u16, key: u64) -> Result<(), Status> {
        ok(unsafe {
            sys::zx_guest_set_trap(
                self.raw_handle(),
                sys::ZX_GUEST_TRAP_IO,
                addr.into(),
                size.into(),
                sys::ZX_HANDLE_INVALID,
                key,
            )
        })
    }
}

// Below are types and implementations for parts of guest trap packets that allow the type safe
// wrappers to provide constrained values.

/// Represents the default operand size as specified by the CS descriptor.
#[derive(Debug, Clone, Copy)]
pub enum CSDefaultOperandSize {
    Bits16 = 2,
    Bits32 = 4,
}

#[derive(Debug, Clone, Copy)]
pub enum MemAccessSize {
    Bits8 = 1,
    Bits16 = 2,
    Bits32 = 4,
    Bits64 = 8,
}

#[derive(Debug, Clone, Copy)]
pub enum PortAccessSize {
    Bits8 = 1,
    Bits16 = 2,
    Bits32 = 4,
}

#[derive(Debug, Clone, Copy)]
pub enum AccessType {
    Read,
    Write,
}

#[derive(Debug, Clone, Copy)]
pub enum PortData {
    Data8(u8),
    Data16(u16),
    Data32(u32),
}
