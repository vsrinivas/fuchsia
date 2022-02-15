// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Zircon bti objects.

use crate::ok;
use crate::{AsHandleRef, Handle, HandleBased, HandleRef, Iommu, Status};
use fuchsia_zircon_sys as sys;

/// An object representing a Zircon Bus Transaction Initiator object.
/// See [BTI Documentation](https://fuchsia.dev/fuchsia-src/reference/kernel_objects/bus_transaction_initiator) for details.
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
#[derive(Debug, Eq, PartialEq, Ord, PartialOrd, Hash)]
#[repr(transparent)]
pub struct Bti(Handle);
impl_handle_based!(Bti);

impl Bti {
    // Create a Bus Transaction Initiator object.
    // Wraps the
    // [`zx_bti_create`](https://fuchsia.dev/fuchsia-src/reference/syscalls/bti_create) system call to create a bti.
    pub fn create(iommu: &Iommu, id: u64) -> Result<Bti, Status> {
        let mut bti_handle = sys::zx_handle_t::default();
        let status = unsafe {
            // SAFETY: regular system call with no unsafe parameters.
            sys::zx_bti_create(iommu.raw_handle(), 0, id, &mut bti_handle)
        };
        ok(status)?;
        unsafe {
            // SAFETY: The syscall docs claim that upon success, bti_handle will be a valid
            // handle to bti object.
            Ok(Bti::from(Handle::from_raw(bti_handle)))
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{Handle, IommuDescDummy, ObjectType, Resource, Vmo};
    use fidl_fuchsia_boot as fboot;
    use fuchsia_component::client::connect_channel_to_protocol;

    #[test]
    fn create_bti_invalid_handle() {
        let status = Bti::create(&Iommu::from(Handle::invalid()), 0);
        assert_eq!(status, Err(Status::BAD_HANDLE));
    }

    #[test]
    fn create_bti_wrong_handle() {
        let vmo = Vmo::create(0).unwrap();
        let wrong_handle = unsafe { Iommu::from(Handle::from_raw(vmo.into_raw())) };

        let status = Bti::create(&wrong_handle, 0);
        assert_eq!(status, Err(Status::WRONG_TYPE));
    }

    fn create_iommu() -> Iommu {
        use fuchsia_zircon::{Channel, HandleBased, Time};
        let (client_end, server_end) = Channel::create().unwrap();
        connect_channel_to_protocol::<fboot::RootResourceMarker>(server_end).unwrap();
        let service = fboot::RootResourceSynchronousProxy::new(client_end);
        let resource = service.get(Time::INFINITE).expect("couldn't get root resource");
        // This test and fuchsia-zircon are different crates, so we need
        // to use from_raw to convert between the fuchsia_zircon handle and this test handle.
        // See https://fxbug.dev/91562 for details.
        let resource = unsafe { Resource::from(Handle::from_raw(resource.into_raw())) };
        Iommu::create_dummy(&resource, IommuDescDummy::default()).unwrap()
    }

    #[test]
    fn create_from_valid_iommu() {
        let iommu = create_iommu();
        let bti = Bti::create(&iommu, 0).unwrap();

        let info = bti.as_handle_ref().basic_info().unwrap();
        assert_eq!(info.object_type, ObjectType::BTI);
    }
}
