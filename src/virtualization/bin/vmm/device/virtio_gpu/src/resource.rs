// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::wire,
    anyhow::{anyhow, Error},
    fidl_fuchsia_ui_composition::BufferCollectionImportToken,
    fuchsia_zircon::{self as zx, HandleBased},
    mapped_vmo,
    virtio_device::mem::DeviceRange,
};

#[cfg(not(test))]
use {
    anyhow::Context,
    fidl_fuchsia_ui_composition::{AllocatorMarker, RegisterBufferCollectionArgs},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_framebuffer::{sysmem::BufferCollectionAllocator, FrameUsage},
    fuchsia_scenic::BufferCollectionTokenPair,
};

/// Returns the size of a pixel (in bytes).
pub const fn bytes_per_pixel() -> usize {
    // All currently known and supported virtio-gpu pixel formats are 4 bytes per pixel.
    4
}

#[cfg(not(test))]
pub const fn sysmem_pixel_format(
    virtio_pixel_format: u32,
) -> Option<fidl_fuchsia_sysmem::PixelFormatType> {
    match virtio_pixel_format {
        wire::VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM | wire::VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM => {
            Some(fidl_fuchsia_sysmem::PixelFormatType::Bgra32)
        }
        wire::VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM | wire::VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM => {
            Some(fidl_fuchsia_sysmem::PixelFormatType::R8G8B8A8)
        }
        _ => None,
    }
}

pub struct Resource2D<'a> {
    id: u32,
    width: u32,
    height: u32,
    mapping: mapped_vmo::Mapping,
    backing: Option<Vec<&'a [u8]>>,
    import_token: Option<BufferCollectionImportToken>,
}

impl<'a> Resource2D<'a> {
    #[cfg(not(test))]
    pub async fn allocate(cmd: &wire::VirtioGpuResourceCreate2d) -> Result<Resource2D<'a>, Error> {
        let pixel_format = if let Some(pixel_format) = sysmem_pixel_format(cmd.format.get()) {
            pixel_format
        } else {
            return Err(anyhow!("Unsupported pixel format {}", cmd.format.get()));
        };
        let mut buffer_allocator = BufferCollectionAllocator::new(
            cmd.width.get(),
            cmd.height.get(),
            pixel_format,
            FrameUsage::Cpu,
            1,
        )
        .context("failed to create BufferCollectionAllocator")?;
        buffer_allocator
            .set_name(100, format!("Virtio GPU Resource {}", cmd.resource_id.get()).as_ref())
            .context("FIDL error setting buffer debug name")?;

        // Register the buffer collection with the Flatland Allocator.
        let buffer_collection_token_for_flatland =
            buffer_allocator.duplicate_token().await.context("error duplicating token")?;
        let buffer_tokens = BufferCollectionTokenPair::new();
        let flatland_registration_args = RegisterBufferCollectionArgs {
            export_token: Some(buffer_tokens.export_token),
            buffer_collection_token: Some(buffer_collection_token_for_flatland),
            ..RegisterBufferCollectionArgs::EMPTY
        };
        let allocator =
            connect_to_protocol::<AllocatorMarker>().expect("error connecting to Scenic allocator");
        allocator
            .register_buffer_collection(flatland_registration_args)
            .await
            .context("FIDL error registering buffer collection")?
            .map_err(|e| anyhow!("error registering buffer collection: {:?}", e))?;

        // Now allocate the buffers with sysmem so that we can get the VMO backing the allocation.
        let mut allocation =
            buffer_allocator.allocate_buffers(true).await.context("buffer allocation failed")?;
        let vmo = &allocation.buffers[0].vmo.take();
        let mapping_flags = zx::VmarFlags::PERM_READ
            | zx::VmarFlags::PERM_WRITE
            | zx::VmarFlags::MAP_RANGE
            | zx::VmarFlags::REQUIRE_NON_RESIZABLE;
        let mapping_size = allocation.settings.buffer_settings.size_bytes as usize;
        let mapping = mapped_vmo::Mapping::create_from_vmo(
            vmo.as_ref().unwrap(),
            mapping_size,
            mapping_flags,
        )?;
        Ok(Self {
            id: cmd.resource_id.get(),
            width: cmd.width.get(),
            height: cmd.height.get(),
            mapping,
            backing: None,
            import_token: Some(buffer_tokens.import_token),
        })
    }

    /// A simple constructor that does not depend on sysmem or flatland for testing purposes only.
    #[cfg(test)]
    pub async fn allocate(cmd: &wire::VirtioGpuResourceCreate2d) -> Result<Resource2D<'a>, Error> {
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
        let (mapping, _vmo) = mapped_vmo::Mapping::allocate(size)?;
        Ok(Self {
            id: cmd.resource_id.get(),
            width: cmd.width.get(),
            height: cmd.height.get(),
            mapping,
            backing: None,
            import_token: None,
        })
    }

    /// Expose the mapping so that tests can read the contents of the host resource.
    #[cfg(test)]
    pub fn mapping(&self) -> &mapped_vmo::Mapping {
        &self.mapping
    }

    pub fn id(&self) -> u32 {
        self.id
    }

    pub fn width(&self) -> u32 {
        self.width
    }

    pub fn height(&self) -> u32 {
        self.height
    }

    /// Returns a sysmem import token for the buffer collection that backs this resource.
    ///
    /// This will return None if the resource was allocated within a unit test environment (where
    /// there is no sysmem available), or if there was an error duplicating the token handle.
    pub fn import_token(&self) -> Option<BufferCollectionImportToken> {
        if let Some(BufferCollectionImportToken { value }) = self.import_token.as_ref() {
            if let Ok(value) = value.duplicate_handle(zx::Rights::SAME_RIGHTS) {
                return Some(BufferCollectionImportToken { value });
            }
        }
        None
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

    pub fn detach_backing(&mut self) {
        self.backing = None
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
    async fn test_attach_backing() {
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
        let mut resource =
            Resource2D::allocate(&resource_create).await.expect("Failed to allocate resource");

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
