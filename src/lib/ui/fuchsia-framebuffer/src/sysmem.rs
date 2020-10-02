// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::FrameUsage;
use anyhow::{format_err, Context, Error};
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
        min_coded_width: 0,
        max_coded_width: 0,
        min_coded_height: 0,
        max_coded_height: 0,
        min_bytes_per_row: 0,
        max_bytes_per_row: 0,
        max_coded_width_times_coded_height: 0,
        layers: 0,
        coded_width_divisor: 0,
        coded_height_divisor: 0,
        bytes_per_row_divisor: 0,
        start_offset_divisor: 0,
        display_width_divisor: 0,
        display_height_divisor: 0,
        required_min_coded_width: width,
        required_max_coded_width: width,
        required_min_coded_height: height,
        required_max_coded_height: height,
        required_min_bytes_per_row: 0,
        required_max_bytes_per_row: 0,
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
        heap_permitted_count: 0,
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
        _ => return Err(format_err!("Unsupported format")),
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
        return Err(format_err!("Invalid width for constraints"));
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
    usage: FrameUsage,
    buffer_count: usize,
    sysmem: fidl_fuchsia_sysmem::AllocatorProxy,
    collection_client: Option<fidl_fuchsia_sysmem::BufferCollectionProxy>,
}

impl BufferCollectionAllocator {
    pub fn new(
        width: u32,
        height: u32,
        pixel_type: PixelFormatType,
        usage: FrameUsage,
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
            usage,
            buffer_count,
            sysmem,
            collection_client: None,
        })
    }

    pub async fn allocate_buffers(
        &mut self,
        set_constraints: bool,
    ) -> Result<fidl_fuchsia_sysmem::BufferCollectionInfo2, Error> {
        let token = self.token.take().expect("token in allocate_buffers");
        let (collection_client, collection_request) =
            create_endpoints::<fidl_fuchsia_sysmem::BufferCollectionMarker>()?;
        self.sysmem.bind_shared_collection(
            ClientEnd::new(token.into_channel().unwrap().into_zx_channel()),
            collection_request,
        )?;
        let collection_client = collection_client.into_proxy()?;
        self.allocate_buffers_proxy(collection_client, set_constraints).await
    }

    async fn allocate_buffers_proxy(
        &mut self,
        collection_client: fidl_fuchsia_sysmem::BufferCollectionProxy,
        set_constraints: bool,
    ) -> Result<fidl_fuchsia_sysmem::BufferCollectionInfo2, Error> {
        let mut buffer_collection_constraints = crate::sysmem::buffer_collection_constraints(
            self.width,
            self.height,
            self.pixel_type,
            self.buffer_count as u32,
            self.usage,
        );
        collection_client
            .set_constraints(set_constraints, &mut buffer_collection_constraints)
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

        self.token
            .as_ref()
            .expect("token in duplicate_token[duplicate]")
            .duplicate(std::u32::MAX, requested_token_request)?;
        self.token.as_ref().expect("tokenin duplicate_token[sync]").sync().await?;
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
    use super::*;
    use fidl_fuchsia_sysmem::{
        AllocatorMarker, AllocatorProxy, BufferCollectionInfo2, BufferCollectionMarker,
        BufferCollectionProxy, BufferCollectionRequest, BufferMemorySettings, CoherencyDomain,
        SingleBufferSettings, VmoBuffer,
    };
    use fuchsia_async as fasync;
    use futures::prelude::*;

    const BUFFER_COUNT: usize = 3;

    fn spawn_allocator_server() -> Result<AllocatorProxy, Error> {
        let (proxy, mut stream) = fidl::endpoints::create_proxy_and_stream::<AllocatorMarker>()?;

        fasync::Task::spawn(async move {
            while let Some(_) = stream.try_next().await.expect("Failed to get request") {}
        })
        .detach();
        Ok(proxy)
    }

    fn spawn_buffer_collection() -> Result<BufferCollectionProxy, Error> {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<BufferCollectionMarker>()?;

        fasync::Task::spawn(async move {
            let mut stored_constraints = None;
            while let Some(req) = stream.try_next().await.expect("Failed to get request") {
                match req {
                    BufferCollectionRequest::SetConstraints {
                        has_constraints,
                        constraints,
                        control_handle: _,
                    } => {
                        if has_constraints {
                            stored_constraints = Some(constraints);
                        } else {
                            stored_constraints = None;
                        }
                    }
                    BufferCollectionRequest::WaitForBuffersAllocated { responder } => {
                        let constraints =
                            stored_constraints.expect("Expected a BufferCollectionRequest!");
                        let mut response = BufferCollectionInfo2 {
                            buffer_count: constraints.min_buffer_count,
                            // Everything below here is unused
                            settings: SingleBufferSettings {
                                buffer_settings: BufferMemorySettings {
                                    size_bytes: 0,
                                    is_physically_contiguous: false,
                                    is_secure: false,
                                    coherency_domain: CoherencyDomain::Cpu,
                                    heap: HeapType::SystemRam,
                                },
                                has_image_format_constraints: false,
                                image_format_constraints: linear_image_format_constraints(
                                    0,
                                    0,
                                    PixelFormatType::Invalid,
                                ),
                            },
                            buffers: [
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                                VmoBuffer { vmo: None, vmo_usable_start: 0 },
                            ],
                        };
                        responder.send(zx::sys::ZX_OK, &mut response).expect("Failed to send");
                    }
                    _ => panic!("Unexpected request"),
                }
            }
        })
        .detach();

        return Ok(proxy);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_buffer_collection_allocator() -> std::result::Result<(), anyhow::Error> {
        let alloc_proxy = spawn_allocator_server()?;
        let buf_proxy = spawn_buffer_collection()?;

        // don't use new() as we want to inject alloc_proxy instead of discovering it.
        let mut bca = BufferCollectionAllocator {
            token: None,
            width: 200,
            height: 200,
            pixel_type: PixelFormatType::Bgra32,
            usage: FrameUsage::Cpu,
            buffer_count: BUFFER_COUNT,
            sysmem: alloc_proxy,
            collection_client: None,
        };

        let buffers = bca.allocate_buffers_proxy(buf_proxy, true).await?;
        assert_eq!(buffers.buffer_count, BUFFER_COUNT as u32);

        Ok(())
    }
}
