// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon iommu objects.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Resource, Status};
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon iommu
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Iommu(Handle);
impl_handle_based!(Iommu);

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct IommuDescDummy {
    reserved: u8,
}

impl Default for IommuDescDummy {
    fn default() -> IommuDescDummy {
        Self::from(sys::zx_iommu_desc_dummy_t::default())
    }
}

impl From<sys::zx_iommu_desc_dummy_t> for IommuDescDummy {
    fn from(desc: sys::zx_iommu_desc_dummy_t) -> IommuDescDummy {
        IommuDescDummy { reserved: desc.reserved }
    }
}

impl From<IommuDescDummy> for sys::zx_iommu_desc_dummy_t {
    fn from(desc: IommuDescDummy) -> sys::zx_iommu_desc_dummy_t {
        sys::zx_iommu_desc_dummy_t { reserved: desc.reserved }
    }
}

impl Iommu {
    // Create an iommu object.
    //
    // Wraps the
    // [`zx_iommu_create`](https://fuchsia.dev/fuchsia-src/reference/syscalls/iommu_create) system call to create an iommu with type `ZX_IOMMU_TYPE_DUMMY`
    pub fn create_dummy(resource: &Resource, desc: IommuDescDummy) -> Result<Iommu, Status> {
        let mut iommu_handle = sys::zx_handle_t::default();
        let mut desc_dummy = sys::zx_iommu_desc_dummy_t::from(desc);
        let status = unsafe {
            // SAFETY:
            //  * desc parameter is a valid pointer (desc_dummy).
            //  * desc_size parameter is the size of desc.
            sys::zx_iommu_create(
                resource.raw_handle(),
                sys::ZX_IOMMU_TYPE_DUMMY,
                &mut desc_dummy as *mut sys::zx_iommu_desc_dummy_t as *const u8,
                std::mem::size_of_val(&desc_dummy),
                &mut iommu_handle,
            )
        };
        ok(status)?;
        unsafe {
            // SAFETY: The syscall docs claim that upon success, iommu_handle will be a valid
            // handle to an iommu object.
            Ok(Iommu::from(Handle::from_raw(iommu_handle)))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Handle, ObjectType, Resource};
    use fidl_fuchsia_boot as fboot;
    use fuchsia_component::client::connect_channel_to_protocol;

    #[test]
    fn iommu_create_invalid_resource() {
        let status =
            Iommu::create_dummy(&Resource::from(Handle::invalid()), IommuDescDummy::default());
        assert_eq!(status, Err(Status::BAD_HANDLE));
    }

    #[test]
    fn iommu_create_from_root_resource() {
        use fuchsia_zircon::{Channel, HandleBased, Time};
        let (client_end, server_end) = Channel::create().unwrap();
        connect_channel_to_protocol::<fboot::RootResourceMarker>(server_end).unwrap();
        let service = fboot::RootResourceSynchronousProxy::new(client_end);
        let resource = service.get(Time::INFINITE).expect("couldn't get root resource");
        // This test and fuchsia-zircon are different crates, so we need
        // to use from_raw to convert between the fuchsia_zircon handle and this test handle.
        // See https://fxbug.dev/91562 for details.
        let resource = unsafe { Resource::from(Handle::from_raw(resource.into_raw())) };
        let iommu = Iommu::create_dummy(&resource, IommuDescDummy::default()).unwrap();
        assert!(!iommu.as_handle_ref().is_invalid());

        let info = iommu.as_handle_ref().basic_info().unwrap();
        assert_eq!(info.object_type, ObjectType::IOMMU);
    }
}
