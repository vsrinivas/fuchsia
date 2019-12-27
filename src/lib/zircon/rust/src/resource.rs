// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon resources.

use crate::ok;
use crate::{object_get_info, ObjectQuery, Topic};
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Status};
use bitflags::bitflags;
use fuchsia_zircon_sys::{self as sys, ZX_MAX_NAME_LEN};

/// An object representing a Zircon resource.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq)]
#[repr(transparent)]
pub struct Resource(Handle);
impl_handle_based!(Resource);

sys::zx_info_resource_t!(ResourceInfo);

impl From<sys::zx_info_resource_t> for ResourceInfo {
    fn from(info: sys::zx_info_resource_t) -> ResourceInfo {
        let sys::zx_info_resource_t { kind, flags, base, size, name } = info;
        ResourceInfo { kind, flags, base, size, name }
    }
}

unsafe impl ObjectQuery for ResourceInfo {
    const TOPIC: Topic = Topic::RESOURCE;
    type InfoTy = ResourceInfo;
}

bitflags! {
    #[repr(transparent)]
    pub struct ResourceKind: sys::zx_rsrc_kind_t {
       const MMIO       = sys::ZX_RSRC_KIND_MMIO;
       const IRQ        = sys::ZX_RSRC_KIND_IRQ;
       const IOPORT     = sys::ZX_RSRC_KIND_IOPORT;
       const HYPERVISOR = sys::ZX_RSRC_KIND_HYPERVISOR;
       const ROOT       = sys::ZX_RSRC_KIND_ROOT;
       const VMEX       = sys::ZX_RSRC_KIND_VMEX;
       const SMC        = sys::ZX_RSRC_KIND_SMC;
    }
}

bitflags! {
    #[repr(transparent)]
    pub struct ResourceFlag: sys::zx_rsrc_flags_t {
       const EXCLUSIVE = sys::ZX_RSRC_FLAG_EXCLUSIVE;
    }
}

impl Resource {
    /// Create a child resource object.
    ///
    /// Wraps the
    /// [zx_resource_create](https://fuchsia.dev/fuchsia-src/reference/syscalls/resource_create.md)
    /// syscall
    pub fn create_child(
        &self,
        kind: ResourceKind,
        flags: Option<ResourceFlag>,
        base: u64,
        size: usize,
        name: &[u8],
    ) -> Result<Resource, Status> {
        let mut resource_out = 0;
        let name_ptr = name.as_ptr();
        let name_len = name.len();
        let flag_bits: u32 = match flags {
            Some(flag) => flag.bits(),
            None => 0,
        };
        let option_bits: u32 = kind.bits() | flag_bits;

        let status = unsafe {
            sys::zx_resource_create(
                self.raw_handle(),
                option_bits,
                base,
                size,
                name_ptr,
                name_len,
                &mut resource_out,
            )
        };
        ok(status)?;
        unsafe { Ok(Resource::from(Handle::from_raw(resource_out))) }
    }

    /// Wraps the
    /// [zx_object_get_info](https://fuchsia.dev/fuchsia-src/reference/syscalls/object_get_info.md)
    /// syscall for the ZX_INFO_RESOURCE topic.
    pub fn info(&self) -> Result<ResourceInfo, Status> {
        let mut info = ResourceInfo::default();
        object_get_info::<ResourceInfo>(self.as_handle_ref(), std::slice::from_mut(&mut info))
            .map(|_| info)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn create_child() {
        let invalid_resource = Resource::from(Handle::invalid());
        assert_eq!(
            invalid_resource.create_child(ResourceKind::VMEX, None, 0, 0, b"vmex"),
            Err(Status::BAD_HANDLE)
        );
    }
}
