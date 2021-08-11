// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon pager objects.

use {
    crate::{ok, AsHandleRef, Handle, HandleBased, HandleRef, Port, Status, Vmo, VmoOptions},
    bitflags::bitflags,
    fuchsia_zircon_sys as sys,
};

/// An object representing a Zircon
/// [pager](https://fuchsia.dev/fuchsia-src/concepts/objects/pager.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Pager(Handle);
impl_handle_based!(Pager);

bitflags! {
    /// Options that may be used when creating a pager.
    #[repr(transparent)]
    pub struct PagerOptions: u32 {
        const _RESERVED = 0; // Placeholder until we add some options.
    }
}

pub enum PagerOp {
    Fail(Status),
}

impl Pager {
    /// See [zx_pager_create](https://https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_create)
    pub fn create(options: PagerOptions) -> Result<Pager, Status> {
        let mut out = 0;
        let status = unsafe { sys::zx_pager_create(options.bits(), &mut out) };
        ok(status)?;
        Ok(Pager::from(unsafe { Handle::from_raw(out) }))
    }

    /// See [zx_pager_create_vmo](https://https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_create_vmo)
    pub fn create_vmo(
        &self,
        options: VmoOptions,
        port: &Port,
        key: u64,
        size: u64,
    ) -> Result<Vmo, Status> {
        let mut out = 0;
        let status = unsafe {
            sys::zx_pager_create_vmo(
                self.raw_handle(),
                options.bits(),
                port.raw_handle(),
                key,
                size,
                &mut out,
            )
        };
        ok(status)?;
        Ok(Vmo::from(unsafe { Handle::from_raw(out) }))
    }

    /// See [zx_pager_detach_vmo](https://https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_detach_vmo)
    pub fn detach_vmo(&self, vmo: &Vmo) -> Result<(), Status> {
        let status = unsafe { sys::zx_pager_detach_vmo(self.raw_handle(), vmo.raw_handle()) };
        ok(status)
    }

    /// See [zx_pager_supply_pages](https://https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_supply_pages)
    pub fn supply_pages(
        &self,
        vmo: &Vmo,
        range: std::ops::Range<u64>,
        aux_vmo: &Vmo,
        aux_offset: u64,
    ) -> Result<(), Status> {
        let status = unsafe {
            sys::zx_pager_supply_pages(
                self.raw_handle(),
                vmo.raw_handle(),
                range.start,
                range.end - range.start,
                aux_vmo.raw_handle(),
                aux_offset,
            )
        };
        ok(status)
    }

    /// See [zx_pager_op_range](https://https://fuchsia.dev/fuchsia-src/reference/syscalls/pager_op_range)
    pub fn op_range(
        &self,
        op: PagerOp,
        pager_vmo: &Vmo,
        range: std::ops::Range<u64>,
    ) -> Result<(), Status> {
        let (op, data) = match op {
            PagerOp::Fail(status) => (sys::ZX_PAGER_OP_FAIL, status.into_raw() as u64),
        };
        let status = unsafe {
            sys::zx_pager_op_range(
                self.raw_handle(),
                op,
                pager_vmo.raw_handle(),
                range.start,
                range.end - range.start,
                data,
            )
        };
        ok(status)
    }
}
