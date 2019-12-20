// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::FrameUsage;
use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_sysmem::{
    BufferCollectionConstraints, BufferMemoryConstraints, BufferUsage, ColorSpace, ColorSpaceType,
    FormatModifier, HeapType, ImageFormatConstraints, PixelFormat as SysmemPixelFormat,
    PixelFormatType,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon::{self as zx, Status};
use std::cmp;

pub fn linear_image_format_constraints(
    width: u32,
    height: u32,
    pixel_type: PixelFormatType,
) -> ImageFormatConstraints {
    ImageFormatConstraints {
        pixel_format: SysmemPixelFormat {
            type_: pixel_type,
            has_format_modifier: true,
            format_modifier: FormatModifier { value: fidl_fuchsia_sysmem::FORMAT_MODIFIER_LINEAR },
        },
        color_spaces_count: 1,
        color_space: [ColorSpace { type_: ColorSpaceType::Srgb }; 32],
        min_coded_width: width,
        max_coded_width: width,
        min_coded_height: height,
        max_coded_height: height,
        min_bytes_per_row: width * 4,
        max_bytes_per_row: width * 4,
        max_coded_width_times_coded_height: width * height,
        layers: 1,
        coded_width_divisor: 1,
        coded_height_divisor: 1,
        bytes_per_row_divisor: 4,
        start_offset_divisor: 1,
        display_width_divisor: 1,
        display_height_divisor: 1,
        required_min_coded_width: width,
        required_max_coded_width: width,
        required_min_coded_height: height,
        required_max_coded_height: height,
        required_min_bytes_per_row: width * 4,
        required_max_bytes_per_row: width * 4,
    }
}

pub fn buffer_memory_constraints(width: u32, height: u32) -> BufferMemoryConstraints {
    BufferMemoryConstraints {
        min_size_bytes: width * height * 4,
        max_size_bytes: std::u32::MAX,
        physically_contiguous_required: false,
        secure_required: false,
        ram_domain_supported: true,
        cpu_domain_supported: true,
        inaccessible_domain_supported: false,
        heap_permitted_count: 1,
        heap_permitted: [HeapType::SystemRam; 32],
    }
}

pub fn buffer_collection_constraints(
    width: u32,
    height: u32,
    pixel_type: PixelFormatType,
    buffer_count: u32,
    frame_usage: FrameUsage,
) -> BufferCollectionConstraints {
    let (usage, has_buffer_memory_constraints, image_format_constraints_count) = match frame_usage {
        FrameUsage::Cpu => (
            BufferUsage {
                none: 0,
                cpu: fidl_fuchsia_sysmem::CPU_USAGE_WRITE_OFTEN
                    | fidl_fuchsia_sysmem::CPU_USAGE_READ_OFTEN,
                vulkan: 0,
                display: 0,
                video: 0,
            },
            true,
            1,
        ),
        FrameUsage::Gpu => {
            (BufferUsage { none: 1, cpu: 0, vulkan: 0, display: 0, video: 0 }, false, 0)
        }
    };
    BufferCollectionConstraints {
        usage: usage,
        min_buffer_count_for_camping: 0,
        min_buffer_count_for_dedicated_slack: 0,
        min_buffer_count_for_shared_slack: 0,
        min_buffer_count: buffer_count,
        max_buffer_count: std::u32::MAX,
        has_buffer_memory_constraints: has_buffer_memory_constraints,
        buffer_memory_constraints: buffer_memory_constraints(width, height),
        image_format_constraints_count: image_format_constraints_count,
        image_format_constraints: [linear_image_format_constraints(width, height, pixel_type); 32],
    }
}

// See ImageFormatStrideBytesPerWidthPixel
pub fn stride_bytes_per_width_pixel(pixel_type: PixelFormatType) -> Result<u32, Error> {
    match pixel_type {
        PixelFormatType::R8G8B8A8 => Ok(4),
        PixelFormatType::Bgra32 => Ok(4),
        PixelFormatType::Bgr24 => Ok(3),
        PixelFormatType::I420 => Ok(1),
        PixelFormatType::M420 => Ok(1),
        PixelFormatType::Nv12 => Ok(1),
        PixelFormatType::Yuy2 => Ok(2),
        PixelFormatType::Yv12 => Ok(1),
        PixelFormatType::Rgb565 => Ok(2),
        PixelFormatType::Rgb332 => Ok(1),
        PixelFormatType::Rgb2220 => Ok(1),
        PixelFormatType::L8 => Ok(1),
        _ => bail!("Unsupported format"),
    }
}

fn round_up_to_align(x: u32, align: u32) -> u32 {
    if align == 0 {
        x
    } else {
        ((x + align - 1) / align) * align
    }
}

// See ImageFormatMinimumRowBytes
pub fn minimum_row_bytes(constraints: ImageFormatConstraints, width: u32) -> Result<u32, Error> {
    if width < constraints.min_coded_width || width > constraints.max_coded_width {
        bail!("Invalid width for constraints");
    }

    let bytes_per_pixel = stride_bytes_per_width_pixel(constraints.pixel_format.type_)?;
    Ok(round_up_to_align(
        cmp::max(bytes_per_pixel * width, constraints.min_bytes_per_row),
        constraints.bytes_per_row_divisor,
    ))
}

pub struct BufferCollectionAllocator {
    token: Option<fidl_fuchsia_sysmem::BufferCollectionTokenProxy>,
    width: u32,
    height: u32,
    pixel_type: PixelFormatType,
    buffer_count: usize,
    sysmem: fidl_fuchsia_sysmem::AllocatorProxy,
    collection_client: Option<fidl_fuchsia_sysmem::BufferCollectionProxy>,
}

impl BufferCollectionAllocator {
    pub fn new(
        width: u32,
        height: u32,
        pixel_type: PixelFormatType,
        buffer_count: usize,
    ) -> Result<BufferCollectionAllocator, Error> {
        let sysmem = connect_to_service::<fidl_fuchsia_sysmem::AllocatorMarker>()?;

        let (local_token, local_token_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>()?;

        sysmem.allocate_shared_collection(local_token_request)?;

        Ok(BufferCollectionAllocator {
            token: Some(local_token.into_proxy()?),
            width,
            height,
            pixel_type,
            buffer_count,
            sysmem,
            collection_client: None,
        })
    }

    pub async fn allocate_buffers(
        &mut self,
    ) -> Result<fidl_fuchsia_sysmem::BufferCollectionInfo2, Error> {
        let token = self.token.take().expect("token");
        let (collection_client, collection_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionMarker>()?;
        self.sysmem.bind_shared_collection(
            ClientEnd::new(token.into_channel().unwrap().into_zx_channel()),
            collection_request,
        )?;
        let collection_client = collection_client.into_proxy()?;
        let mut buffer_collection_constraints = crate::sysmem::buffer_collection_constraints(
            self.width,
            self.height,
            self.pixel_type,
            self.buffer_count as u32,
            FrameUsage::Cpu,
        );
        collection_client
            .set_constraints(true, &mut buffer_collection_constraints)
            .context("Sending buffer constraints to sysmem")?;
        let (status, buffers) = collection_client.wait_for_buffers_allocated().await?;
        self.collection_client = Some(collection_client);
        if status != zx::sys::ZX_OK {
            return Err(format_err!(
                "Failed to wait for buffers {}({})",
                Status::from_raw(status),
                status
            ));
        }
        Ok(buffers)
    }

    pub async fn duplicate_token(
        &mut self,
    ) -> Result<ClientEnd<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>, Error> {
        let (requested_token, requested_token_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionTokenMarker>()?;

        self.token.as_ref().expect("token").duplicate(std::u32::MAX, requested_token_request)?;
        self.token.as_ref().expect("token").sync().await?;
        Ok(requested_token)
    }
}

impl Drop for BufferCollectionAllocator {
    fn drop(&mut self) {
        if let Some(collection_client) = self.collection_client.as_mut() {
            collection_client
                .close()
                .unwrap_or_else(|err| eprintln!("collection_client.close failed with {}", err));
        }
    }
}

#[cfg(test)]
mod test {
    use crate::sysmem::BufferCollectionAllocator;
    use fuchsia_async as fasync;

    const BUFFER_COUNT: usize = 3;

    #[test]
    fn test_buffer_collection_allocator() -> std::result::Result<(), failure::Error> {
        let mut executor = fasync::Executor::new()?;
        let mut bca = BufferCollectionAllocator::new(
            200,
            200,
            fidl_fuchsia_sysmem::PixelFormatType::Bgra32,
            BUFFER_COUNT,
        )?;

        let allocate_future = bca.allocate_buffers();
        let buffers = executor.run_singlethreaded(allocate_future)?;
        assert_eq!(buffers.buffer_count, BUFFER_COUNT as u32);

        Ok(())
    }
}
