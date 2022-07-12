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

/// Returns the size of a pixel (in bytes).
pub const fn bytes_per_pixel() -> usize {
    // All currently known and supported virtio-gpu pixel formats are 4 bytes per pixel.
    4
}

pub struct Resource2D<'a> {
    width: u32,
    height: u32,
    mapping: mapped_vmo::Mapping,
    backing: Option<Vec<&'a [u8]>>,
}

impl<'a> Resource2D<'a> {
    pub fn allocate_from_request(
        cmd: &wire::VirtioGpuResourceCreate2d,
    ) -> Result<(Self, zx::Vmo), Error> {
        // Eventually we will want to use sysmem to allocate the buffers.
        let width: usize = cmd.width.get().try_into()?;
        let height: usize = cmd.height.get().try_into()?;
        let size = width
            .checked_mul(height)
            .map(|size| size.checked_mul(bytes_per_pixel()))
            .flatten()
            .ok_or_else(|| {
                anyhow!("Overflow computing buffer size for resource {}x{}", width, height)
            })?;
        let (mapping, vmo) = mapped_vmo::Mapping::allocate(size)?;
        Ok((Self { width: cmd.width.get(), height: cmd.height.get(), mapping, backing: None }, vmo))
    }

    // Expose the mapping so that tests can inspect the contents of the resource.
    #[cfg(test)]
    pub fn mapping(&self) -> &mapped_vmo::Mapping {
        &self.mapping
    }

    #[cfg(test)]
    pub fn width(&self) -> u32 {
        self.width
    }

    #[cfg(test)]
    pub fn height(&self) -> u32 {
        self.height
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

    fn compute_expected_offset(
        &self,
        rect: &wire::VirtioGpuRect,
    ) -> Result<u64, wire::VirtioGpuError> {
        let rect_x = u64::from(rect.x.get());
        let rect_y = u64::from(rect.y.get());
        let width = u64::from(self.width);
        let bpp = u64::try_from(bytes_per_pixel()).unwrap();
        let expected_offset =
            rect_y.checked_mul(width).ok_or(wire::VirtioGpuError::InvalidParameter)?;
        let expected_offset =
            expected_offset.checked_add(rect_x).ok_or(wire::VirtioGpuError::InvalidParameter)?;
        let expected_offset =
            expected_offset.checked_mul(bpp).ok_or(wire::VirtioGpuError::InvalidParameter)?;
        Ok(expected_offset)
    }

    fn verify_rect_extents(&self, rect: &wire::VirtioGpuRect) -> Result<(), wire::VirtioGpuError> {
        let x_end = rect
            .x
            .get()
            .checked_add(rect.width.get())
            .ok_or(wire::VirtioGpuError::InvalidParameter)?;
        let y_end = rect
            .y
            .get()
            .checked_add(rect.height.get())
            .ok_or(wire::VirtioGpuError::InvalidParameter)?;
        if x_end > self.width || y_end > self.height {
            return Err(wire::VirtioGpuError::InvalidParameter);
        }
        Ok(())
    }

    /// Performs a TRANSFER_TO_HOST_2D operation by transferring the given `rect` from this
    /// resources backing memory into the VMO.
    pub fn transfer_to_host_2d(
        &self,
        offset: u64,
        rect: &wire::VirtioGpuRect,
    ) -> Result<(), wire::VirtioGpuError> {
        // The driver provides a resource offset (in bytes) and a target rect. The spec is not
        // clear if offset can be something other than the computed offset of the x,y in the target
        // rect and all current drivers follow this convention.
        let expected_offset = self.compute_expected_offset(rect)?;
        if offset != expected_offset {
            tracing::warn!(
                "Invalid resource offset received, offset: {}, rect: {:?}",
                offset,
                rect
            );
            return Err(wire::VirtioGpuError::InvalidParameter);
        }

        // Verify that the requested rect lies fully within this resource.
        self.verify_rect_extents(rect)?;

        // Grab a ref to the backing memory. It's an error to attempt a transfer without any
        // backing memory.
        let backing_slices = match self.backing.as_ref() {
            Some(regions) => regions,
            None => {
                tracing::warn!("transfer_to_host_2d without backing attached");
                return Err(wire::VirtioGpuError::Unspecified);
            }
        };

        let rect_row_bytes = usize::try_from(rect.width.get())
            .unwrap()
            .checked_mul(bytes_per_pixel())
            .ok_or(wire::VirtioGpuError::InvalidParameter)?;
        let image_row_bytes = usize::try_from(self.width)
            .unwrap()
            .checked_mul(bytes_per_pixel())
            .ok_or(wire::VirtioGpuError::InvalidParameter)?;
        let mut transfer_bytes_remaining = rect_row_bytes
            .checked_mul(rect.height.get().try_into().unwrap())
            .ok_or(wire::VirtioGpuError::InvalidParameter)?;
        let mut rect_row_bytes_remaining = rect_row_bytes;
        let mut entry_off = 0usize;
        let mut offset: usize = usize::try_from(offset).unwrap();

        for slice in backing_slices.iter() {
            if transfer_bytes_remaining == 0 {
                break;
            }
            while entry_off + slice.len() > offset && transfer_bytes_remaining > 0 {
                // Current entry covers requested content.
                let mut copy_size: usize =
                    std::cmp::min((entry_off + slice.len()) - offset, transfer_bytes_remaining);
                let mut off_next = offset + copy_size;

                // If the copy rect width does not match the resource width, additional
                // logic is required to skip data between rows.
                if rect.width.get() != self.width {
                    if rect_row_bytes_remaining <= copy_size {
                        // Clamp the copy size to the rect row size.
                        copy_size = rect_row_bytes_remaining;
                        // Set the next offset to the start of the next image row.
                        off_next =
                            (offset + image_row_bytes + rect_row_bytes_remaining) - rect_row_bytes;
                        // Reset remaining bytes in the rect row.
                        rect_row_bytes_remaining = rect_row_bytes;
                    } else {
                        rect_row_bytes_remaining -= copy_size;
                    }
                }

                // Do the copy
                let slice_offset = offset - entry_off;
                self.mapping.write_at(offset, &slice[slice_offset..slice_offset + copy_size]);
                transfer_bytes_remaining -= copy_size;
                offset = off_next;
            }
            entry_off += slice.len();
        }
        Ok(())
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
