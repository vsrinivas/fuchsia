// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon vmar objects.

use {ok, sys};
use {AsHandleRef, Handle, HandleBased, HandleRef, Status, Unowned, Vmo};

/// An object representing a Zircon
/// [virtual memory address region](https://fuchsia.googlesource.com/zircon/+/master/docs/objects/vm_address_region.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Vmar(Handle);
impl_handle_based!(Vmar);

impl Vmar {
    pub fn root_self() -> Unowned<'static, Vmar> {
        unsafe {
            let handle = sys::zx_vmar_root_self();
            Unowned::from_raw_handle(handle)
        }
    }

    pub fn allocate(
        &self, offset: usize, size: usize, flags: u32,
    ) -> Result<(Vmar, usize), Status> {
        let mut mapped = 0;
        let mut handle = 0;
        let status = unsafe {
            sys::zx_vmar_allocate(
                self.raw_handle(),
                offset,
                size,
                flags,
                &mut handle,
                &mut mapped,
            )
        };
        ok(status)?;
        unsafe { Ok((Vmar::from(Handle::from_raw(handle)), mapped)) }
    }

    pub fn map(
        &self, vmar_offset: usize, vmo: &Vmo, vmo_offset: u64, len: usize, flags: u32,
    ) -> Result<usize, Status> {
        let mut mapped = 0;
        unsafe {
            let status = sys::zx_vmar_map(
                self.0.raw_handle(),
                vmar_offset,
                vmo.raw_handle(),
                vmo_offset,
                len,
                flags,
                &mut mapped,
            );
            ok(status).map(|_| mapped)
        }
    }

    pub fn unmap(&self, addr: usize, len: usize) -> Result<(), Status> {
        let status = unsafe { sys::zx_vmar_unmap(self.0.raw_handle(), addr, len) };
        ok(status)
    }

    pub fn protect(&self, addr: usize, len: usize, flags: u32) -> Result<(), Status> {
        let status = unsafe { sys::zx_vmar_protect(self.raw_handle(), addr, len, flags) };
        ok(status)
    }

    pub fn destroy(&self) -> Result<(), Status> {
        let status = unsafe { sys::zx_vmar_destroy(self.raw_handle()) };
        ok(status)
    }
}

bitflags! {
    /// Flags to VMAR routines
    #[repr(transparent)]
    pub struct VmarFlags: u32 {
        const PERM_READ          = sys::ZX_VM_FLAG_PERM_READ;
        const PERM_WRITE         = sys::ZX_VM_FLAG_PERM_WRITE;
        const PERM_EXECUTE       = sys::ZX_VM_FLAG_PERM_EXECUTE;
        const COMPACT            = sys::ZX_VM_FLAG_COMPACT;
        const SPECIFIC           = sys::ZX_VM_FLAG_SPECIFIC;
        const SPECIFIC_OVERWRITE = sys::ZX_VM_FLAG_SPECIFIC_OVERWRITE;
        const CAN_MAP_SPECIFIC   = sys::ZX_VM_FLAG_CAN_MAP_SPECIFIC;
        const CAN_MAP_READ       = sys::ZX_VM_FLAG_CAN_MAP_READ;
        const CAN_MAP_WRITE      = sys::ZX_VM_FLAG_CAN_MAP_WRITE;
        const CAN_MAP_EXECUTE    = sys::ZX_VM_FLAG_CAN_MAP_EXECUTE;
    }
}
