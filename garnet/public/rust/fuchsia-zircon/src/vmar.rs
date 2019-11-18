// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon vmar objects.

use crate::ok;
use crate::{object_get_info, ObjectQuery, Topic};
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Status, Vmo};
use bitflags::bitflags;
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon
/// [virtual memory address region](https://fuchsia.dev/fuchsia-src/concepts/objects/vm_address_region.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Vmar(Handle);
impl_handle_based!(Vmar);

sys::zx_info_vmar_t!(VmarInfo);

impl From<sys::zx_info_vmar_t> for VmarInfo {
    fn from(sys::zx_info_vmar_t { base, len }: sys::zx_info_vmar_t) -> VmarInfo {
        VmarInfo { base, len }
    }
}

// VmarInfo is able to be safely replaced with a byte representation and is a PoD type.
unsafe impl ObjectQuery for VmarInfo {
    const TOPIC: Topic = Topic::VMAR;
    type InfoTy = VmarInfo;
}

impl Vmar {
    pub fn allocate(
        &self,
        offset: usize,
        size: usize,
        flags: VmarFlags,
    ) -> Result<(Vmar, usize), Status> {
        let mut mapped = 0;
        let mut handle = 0;
        let status = unsafe {
            sys::zx_vmar_allocate(
                self.raw_handle(),
                flags.bits(),
                offset,
                size,
                &mut handle,
                &mut mapped,
            )
        };
        ok(status)?;
        unsafe { Ok((Vmar::from(Handle::from_raw(handle)), mapped)) }
    }

    pub fn map(
        &self,
        vmar_offset: usize,
        vmo: &Vmo,
        vmo_offset: u64,
        len: usize,
        flags: VmarFlags,
    ) -> Result<usize, Status> {
        let flags = VmarFlagsExtended::from_bits_truncate(flags.bits());
        unsafe { self.map_unsafe(vmar_offset, vmo, vmo_offset, len, flags) }
    }

    /// Directly call zx_vmar_map.
    ///
    /// # Safety
    ///
    /// This function is unsafe because certain flags to `zx_vmar_map` may
    /// replace an existing mapping which is referenced elsewhere.
    pub unsafe fn map_unsafe(
        &self,
        vmar_offset: usize,
        vmo: &Vmo,
        vmo_offset: u64,
        len: usize,
        flags: VmarFlagsExtended,
    ) -> Result<usize, Status> {
        let mut mapped = 0;
        let status = sys::zx_vmar_map(
            self.0.raw_handle(),
            flags.bits(),
            vmar_offset,
            vmo.raw_handle(),
            vmo_offset,
            len,
            &mut mapped,
        );
        ok(status).map(|_| mapped)
    }

    pub unsafe fn unmap(&self, addr: usize, len: usize) -> Result<(), Status> {
        ok(sys::zx_vmar_unmap(self.0.raw_handle(), addr, len))
    }

    pub unsafe fn protect(&self, addr: usize, len: usize, flags: VmarFlags) -> Result<(), Status> {
        ok(sys::zx_vmar_protect(self.raw_handle(), flags.bits(), addr, len))
    }

    pub unsafe fn destroy(&self) -> Result<(), Status> {
        ok(sys::zx_vmar_destroy(self.raw_handle()))
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_VMAR topic.
    pub fn info(&self) -> Result<VmarInfo, Status> {
        let mut info = VmarInfo::default();
        object_get_info::<VmarInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }
}

// TODO(smklein): Ideally we would have two separate sets of bitflags,
// and a union of both of them.
macro_rules! vmar_flags {
    (
        safe: [$($safe_name:ident : $safe_sys_name:ident,)*],
        extended: [$($ex_name:ident : $ex_sys_name:ident,)*],
    ) => {
        bitflags! {
            /// Flags to VMAR routines which are considered safe.
            #[repr(transparent)]
            pub struct VmarFlags: sys::zx_vm_option_t {
                $(
                    const $safe_name = sys::$safe_sys_name;
                )*
            }
        }

        bitflags! {
            /// Flags to all VMAR routines.
            #[repr(transparent)]
            pub struct VmarFlagsExtended: sys::zx_vm_option_t {
                $(
                    const $safe_name = sys::$safe_sys_name;
                )*
                $(
                    const $ex_name = sys::$ex_sys_name;
                )*
            }
        }
    };
}

vmar_flags! {
    safe: [
        PERM_READ: ZX_VM_PERM_READ,
        PERM_WRITE: ZX_VM_PERM_WRITE,
        PERM_EXECUTE: ZX_VM_PERM_EXECUTE,
        COMPACT: ZX_VM_COMPACT,
        SPECIFIC: ZX_VM_SPECIFIC,
        CAN_MAP_SPECIFIC: ZX_VM_CAN_MAP_SPECIFIC,
        CAN_MAP_READ: ZX_VM_CAN_MAP_READ,
        CAN_MAP_WRITE: ZX_VM_CAN_MAP_WRITE,
        CAN_MAP_EXECUTE: ZX_VM_CAN_MAP_EXECUTE,
        MAP_RANGE: ZX_VM_MAP_RANGE,
        REQUIRE_NON_RESIZABLE: ZX_VM_REQUIRE_NON_RESIZABLE,
    ],
    extended: [
        SPECIFIC_OVERWRITE: ZX_VM_SPECIFIC_OVERWRITE,
    ],
}

#[cfg(test)]
mod tests {
    // The unit tests are built with a different crate name, but fuchsia_runtime returns a "real"
    // fuchsia_zircon::Vmar that we need to use.
    use fuchsia_zircon::{Status, VmarFlags};

    #[test]
    fn allocate_and_info() -> Result<(), Status> {
        let size = usize::pow(2, 20); // 1MiB
        let root_vmar = fuchsia_runtime::vmar_root_self();
        let (vmar, base) = root_vmar.allocate(0, size, VmarFlags::empty())?;

        let info = vmar.info()?;
        assert!(info.base == base);
        assert!(info.len == size);
        Ok(())
    }

    #[test]
    fn root_vmar_info() -> Result<(), Status> {
        let root_vmar = fuchsia_runtime::vmar_root_self();
        let info = root_vmar.info()?;
        assert!(info.base > 0);
        assert!(info.len > 0);
        Ok(())
    }
}
