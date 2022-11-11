// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{convert::TryInto, io::Read};

use anyhow::{anyhow, Error};
use fidl::endpoints::{create_endpoints, create_proxy, ClientEnd, Proxy};
use fidl_fuchsia_math as fmath;
use fidl_fuchsia_sysmem as sysmem;
use fidl_fuchsia_ui_composition as ui_comp;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_image_format::{
    BUFFER_COLLECTION_CONSTRAINTS_DEFAULT, BUFFER_MEMORY_CONSTRAINTS_DEFAULT, BUFFER_USAGE_DEFAULT,
    IMAGE_FORMAT_CONSTRAINTS_DEFAULT,
};
use fuchsia_scenic::{duplicate_buffer_collection_token, BufferCollectionTokenPair};
use fuchsia_zircon as zx;
use mapped_vmo::Mapping;
use png;

pub struct Image {
    flatland: ui_comp::FlatlandProxy,
    content_id: ui_comp::ContentId,
}

impl Image {
    pub(crate) fn new(
        image_data: &mut ImageData,
        flatland: ui_comp::FlatlandProxy,
        mut content_id: ui_comp::ContentId,
    ) -> Result<Image, Error> {
        flatland.create_image(
            &mut content_id,
            &mut image_data.import_token,
            image_data.vmo_index,
            ui_comp::ImageProperties {
                size: Some(fmath::SizeU { width: image_data.width, height: image_data.height }),
                ..ui_comp::ImageProperties::EMPTY
            },
        )?;

        Ok(Image { flatland: flatland.clone(), content_id })
    }

    pub fn get_content_id(&self) -> ui_comp::ContentId {
        self.content_id.clone()
    }

    pub fn set_size(&self, width: u32, height: u32) -> Result<(), Error> {
        let mut content_id = self.get_content_id();
        let mut size = fmath::SizeU { width, height };
        self.flatland.set_image_destination_size(&mut content_id, &mut size)?;
        Ok(())
    }
}

#[derive(Debug)]
pub struct ImageData {
    pub width: u32,
    pub height: u32,
    pub pixel_format: sysmem::PixelFormatType,
    pub import_token: ui_comp::BufferCollectionImportToken,
    pub vmo_index: u32,
}

/// Loads a PNG image from a [Read] reader and returns it's data, width and height.
pub fn load_png<R: Read>(r: R) -> Result<(Vec<u8>, u32, u32), Error> {
    let decoder = png::Decoder::new(r);
    let (info, mut reader) = decoder.read_info()?;

    // We only support png::ColorType::RGBA.
    if info.color_type != png::ColorType::RGBA || info.bit_depth != png::BitDepth::Eight {
        return Err(anyhow!(
            "Cannot load PNG with pixel format: {:?}, only 32-bit RGBA format is supported.",
            info.color_type
        ));
    }

    let mut bytes = vec![0; info.buffer_size()];
    reader.next_frame(&mut bytes)?;

    Ok((bytes, info.width, info.height))
}

/// Load's an image from bytes, width and height into a `BufferCollection` and returns [ImageData].
/// Needs `fuchsia.sysmem.Allocator` and `fuchsia.ui.composition.Allocator` capabilities routed to
/// the component calling this function.
pub async fn load_image_from_bytes(
    bytes: &[u8],
    width: u32,
    height: u32,
) -> Result<ImageData, Error> {
    let sysmem_allocator = connect_to_protocol::<sysmem::AllocatorMarker>()?;
    let flatland_allocator = connect_to_protocol::<ui_comp::AllocatorMarker>()?;
    load_image_from_bytes_using_allocators(
        bytes,
        width,
        height,
        sysmem_allocator,
        flatland_allocator,
    )
    .await
}

pub async fn load_image_from_bytes_using_allocators(
    bytes: &[u8],
    width: u32,
    height: u32,
    sysmem_allocator: sysmem::AllocatorProxy,
    flatland_allocator: ui_comp::AllocatorProxy,
) -> Result<ImageData, Error> {
    // We only support 32-bit RGBA formatted images.
    if bytes.len() as u32 != width * height * 4 {
        return Err(anyhow!("Invalid image data. Only 32 bit RGBA formatted image supported"));
    }
    let pixel_format = sysmem::PixelFormatType::R8G8B8A8;

    // Allocate shared buffers.
    let (buffer_collection_token, buffer_collection_token_server_end) =
        create_endpoints::<sysmem::BufferCollectionTokenMarker>()?;
    sysmem_allocator.allocate_shared_collection(buffer_collection_token_server_end)?;

    // Duplicate buffer collection token for [ui_comp::Allocator].
    let mut buffer_collection_token = buffer_collection_token.into_proxy()?;
    let buffer_collection_token_for_flatland =
        duplicate_buffer_collection_token(&mut buffer_collection_token).await?;
    let buffer_collection_token = ClientEnd::<sysmem::BufferCollectionTokenMarker>::new(
        buffer_collection_token.into_channel().expect("FIDL error").into_zx_channel(),
    );

    // Bind shared buffers.
    let (buffer_collection, buffer_collection_server_end) =
        create_proxy::<sysmem::BufferCollectionMarker>()?;
    sysmem_allocator
        .bind_shared_collection(buffer_collection_token, buffer_collection_server_end)?;

    // Set buffer constraints.
    buffer_collection
        .set_constraints(true, &mut buffer_collection_constraints(width, height, pixel_format))?;

    // Register buffers with [ui_comp::Allocator].
    let buffer_collection_token_pair = BufferCollectionTokenPair::new();
    let args = ui_comp::RegisterBufferCollectionArgs {
        export_token: Some(buffer_collection_token_pair.export_token),
        buffer_collection_token: Some(buffer_collection_token_for_flatland),
        ..ui_comp::RegisterBufferCollectionArgs::EMPTY
    };
    let _ = flatland_allocator.register_buffer_collection(args).await?;

    // Wait for allocation.
    let collection_info = {
        let (status, info) = buffer_collection.wait_for_buffers_allocated().await?;
        let () = zx::Status::ok(status)?;
        info
    };
    buffer_collection.close()?;

    // Get the allocated VMO.
    if collection_info.buffer_count != 1 {
        return Err(anyhow!("Failed to allocate buffer for image."));
    }

    let size: usize = collection_info.settings.buffer_settings.size_bytes.try_into()?;
    let vmo = collection_info.buffers[0]
        .vmo
        .as_ref()
        .expect("Failed to extract VMO from buffer collection");

    // Write bytes to VMO.
    let mapping =
        Mapping::create_from_vmo(vmo, size, zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE)?;
    mapping.write(bytes);

    // Flush VMO if needed.
    if collection_info.settings.buffer_settings.coherency_domain == sysmem::CoherencyDomain::Ram {
        vmo.op_range(zx::VmoOp::CACHE_CLEAN, 0, vmo.get_size()?)?;
    }

    // Return image data with buffer import token.

    Ok(ImageData {
        width,
        height,
        pixel_format,
        import_token: buffer_collection_token_pair.import_token,
        vmo_index: 0,
    })
}

fn buffer_collection_constraints(
    width: u32,
    height: u32,
    pixel_format: sysmem::PixelFormatType,
) -> sysmem::BufferCollectionConstraints {
    let usage = sysmem::BufferUsage {
        cpu: sysmem::CPU_USAGE_READ_OFTEN | sysmem::CPU_USAGE_WRITE_OFTEN,
        ..BUFFER_USAGE_DEFAULT
    };

    let buffer_memory_constraints = sysmem::BufferMemoryConstraints {
        ram_domain_supported: true,
        cpu_domain_supported: true,
        ..BUFFER_MEMORY_CONSTRAINTS_DEFAULT
    };

    let pixel_format = sysmem::PixelFormat {
        type_: pixel_format,
        has_format_modifier: true,
        format_modifier: sysmem::FormatModifier { value: sysmem::FORMAT_MODIFIER_LINEAR },
    };

    let mut image_constraints = sysmem::ImageFormatConstraints {
        required_max_coded_width: width,
        required_max_coded_height: height,
        color_spaces_count: 1,
        pixel_format,
        ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
    };

    // TODO(fxb/112715): Check if image conforms to Srgb colorspace before setting this constraint.
    image_constraints.color_space[0].type_ = sysmem::ColorSpaceType::Srgb;

    let mut constraints = sysmem::BufferCollectionConstraints {
        min_buffer_count: 1,
        usage,
        has_buffer_memory_constraints: true,
        buffer_memory_constraints,
        image_format_constraints_count: 1,
        ..BUFFER_COLLECTION_CONSTRAINTS_DEFAULT
    };
    constraints.image_format_constraints[0] = image_constraints;

    constraints
}
