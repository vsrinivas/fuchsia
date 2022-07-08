// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire,
    anyhow::{anyhow, Error},
    fuchsia_zircon::{self as zx},
    mapped_vmo,
    virtio_device::mem::DeviceRange,
};

pub struct Resource2D<'a> {
    _mapping: mapped_vmo::Mapping,
    backing: Option<Vec<&'a [u8]>>,
}

impl<'a> Resource2D<'a> {
    pub fn allocate_from_request(
        cmd: &wire::VirtioGpuResourceCreate2d,
    ) -> Result<(Self, zx::Vmo), Error> {
        // Eventually we will want to use sysmem to allocate the buffers.
        let width: usize = cmd.width.get().try_into()?;
        let height: usize = cmd.height.get().try_into()?;
        let size = width.checked_mul(height).ok_or_else(|| {
            anyhow!("Overflow computing buffer size for resource {}x{}", width, height)
        })?;
        let (mapping, vmo) = mapped_vmo::Mapping::allocate(size)?;
        Ok((Self { _mapping: mapping, backing: None }, vmo))
    }

    pub fn attach_backing(&mut self, ranges: Vec<DeviceRange<'a>>) {
        self.backing = Some(
            ranges
                .iter()
                .map(|range| unsafe {
                    // SAFETY: We unwrap on the pointer because try_ptr requires that the range is
                    // large enough to hold T and it has the correct alignment for T.
                    //
                    // Since T in this situation is 'u8' these will both always be true and we
                    // never expect `try_ptr` to fail.
                    //
                    // We retain the pointer into guest memory for as long as `Resource2D` is valid
                    // or until the backing pages are changed for this resource. We will only
                    // dereference this memory when performing a TRANSFER_TO_HOST_2D operation,
                    // which will copy bytes from these slices into another VMO that will be shared
                    // with Flatland. Any modifications to this memory after the start of a
                    // TRANSFER_TO_HOST_2D command and before it is completed could result in
                    // graphical artifacts presented on the Framebuffer but does not pose a
                    // security or memory safety risk.
                    std::slice::from_raw_parts(range.try_ptr().unwrap(), range.len())
                })
                .collect(),
        );
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::wire, virtio_device::fake_queue::IdentityDriverMem};

    #[fuchsia::test]
    fn test_attach_backing() {
        const RESOURCE_ID: u32 = 1;
        const RESOURCE_WIDTH: u32 = 1024;
        const RESOURCE_HEIGHT: u32 = 768;

        // Create a resource.
        let resource_create = wire::VirtioGpuResourceCreate2d {
            resource_id: RESOURCE_ID.into(),
            width: RESOURCE_WIDTH.into(),
            height: RESOURCE_HEIGHT.into(),
            format: wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM.into(),
        };
        let (mut resource, _) = Resource2D::allocate_from_request(&resource_create)
            .expect("Failed to allocate resource");

        // Attach backing memory.
        let mem = IdentityDriverMem::new();
        let range_size = (RESOURCE_WIDTH * RESOURCE_HEIGHT / 2).try_into().unwrap();
        let backing = vec![mem.new_range(range_size).unwrap(), mem.new_range(range_size).unwrap()];
        resource.attach_backing(backing.clone());

        let slices = resource.backing.as_ref().unwrap();
        assert_eq!(2, slices.len());
        assert_eq!(backing[0].get().start, slices[0].as_ptr() as usize);
        assert_eq!(backing[0].get().len(), slices[0].len());
        assert_eq!(backing[1].get().start, slices[1].as_ptr() as usize);
        assert_eq!(backing[1].get().len(), slices[1].len());
    }
}
