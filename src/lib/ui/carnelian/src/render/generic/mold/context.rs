// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, collections::HashMap, mem, u32};

use euclid::{Rect, Size2D};
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_sysmem::{
    AllocatorMarker, BufferCollectionConstraints, BufferCollectionSynchronousProxy,
    BufferCollectionTokenMarker, BufferMemoryConstraints, BufferUsage, ColorSpace, ColorSpaceType,
    FormatModifier, HeapType, ImageFormatConstraints, PixelFormat as SysmemPixelFormat,
    PixelFormatType, CPU_USAGE_WRITE_OFTEN, FORMAT_MODIFIER_LINEAR,
};
use fuchsia_component::client::connect_to_service;
use fuchsia_framebuffer::PixelFormat;
use fuchsia_zircon as zx;

use crate::{
    render::generic::{
        mold::{
            image::VmoImage, Mold, MoldComposition, MoldImage, MoldPathBuilder, MoldRasterBuilder,
        },
        Context, CopyRegion, PostCopy, PreClear, PreCopy, RenderExt,
    },
    ViewAssistantContext,
};

fn buffer_collection_constraints(width: u32, height: u32) -> BufferCollectionConstraints {
    let image_format_constraints = ImageFormatConstraints {
        pixel_format: SysmemPixelFormat {
            type_: PixelFormatType::Bgra32,
            has_format_modifier: true,
            format_modifier: FormatModifier { value: FORMAT_MODIFIER_LINEAR },
        },
        color_spaces_count: 1,
        color_space: [ColorSpace { type_: ColorSpaceType::Srgb }; 32],
        min_coded_width: width,
        max_coded_width: u32::MAX,
        min_coded_height: height,
        max_coded_height: u32::MAX,
        min_bytes_per_row: width * mem::size_of::<u32>() as u32,
        max_bytes_per_row: u32::MAX,
        max_coded_width_times_coded_height: u32::MAX,
        layers: 1,
        coded_width_divisor: 1,
        coded_height_divisor: 1,
        bytes_per_row_divisor: mem::size_of::<u32>() as u32,
        start_offset_divisor: 1,
        display_width_divisor: 1,
        display_height_divisor: 1,
        required_min_coded_width: 0,
        required_max_coded_width: 0,
        required_min_coded_height: 0,
        required_max_coded_height: 0,
        required_min_bytes_per_row: 0,
        required_max_bytes_per_row: 0,
    };

    BufferCollectionConstraints {
        usage: BufferUsage { none: 0, cpu: CPU_USAGE_WRITE_OFTEN, vulkan: 0, display: 0, video: 0 },
        min_buffer_count_for_camping: 0,
        min_buffer_count_for_dedicated_slack: 0,
        min_buffer_count_for_shared_slack: 0,
        min_buffer_count: 1,
        max_buffer_count: u32::MAX,
        has_buffer_memory_constraints: true,
        buffer_memory_constraints: BufferMemoryConstraints {
            min_size_bytes: width * height * mem::size_of::<u32>() as u32,
            max_size_bytes: u32::MAX,
            physically_contiguous_required: false,
            secure_required: false,
            ram_domain_supported: true,
            cpu_domain_supported: true,
            inaccessible_domain_supported: false,
            heap_permitted_count: 1,
            heap_permitted: [HeapType::SystemRam; 32],
        },
        image_format_constraints_count: 1,
        image_format_constraints: [image_format_constraints; 32],
    }
}

fn copy_region_to_image(
    src: &mut [u8],
    src_width: usize,
    src_height: usize,
    src_bytes_per_row: usize,
    dst: &mut [u8],
    dst_bytes_per_row: usize,
    region: &CopyRegion,
) {
    let (mut y, dy) = if region.dst_offset.y < region.src_offset.y {
        // Copy forward.
        (0, 1)
    } else {
        // Copy backwards.
        (region.extent.height as i32 - 1, -1)
    };

    let mut extent = region.extent.height;
    while extent > 0 {
        let src_y = (region.src_offset.y + y as u32) % src_height as u32;
        let dst_y = region.dst_offset.y + y as u32;

        let mut src_x = region.src_offset.x as usize;
        let mut dst_x = region.dst_offset.x as usize;
        let mut width = region.extent.width as usize;

        while width > 0 {
            let columns = width.min(src_width - src_x);
            let src_offset = src_y as usize * src_bytes_per_row + src_x * 4;
            let dst_offset = dst_y as usize * dst_bytes_per_row + dst_x * 4;

            assert!((dst_offset + dst_bytes_per_row) <= dst.len());
            dst[dst_offset..dst_offset + mem::size_of::<u32>()]
                .copy_from_slice(&src[src_offset..src_offset + mem::size_of::<u32>()]);

            width -= columns;
            dst_x += columns;
            src_x = 0;
        }

        y += dy;
        extent -= 1;
    }
}

#[derive(Debug)]
pub struct MoldContext {
    buffer_collection: BufferCollectionSynchronousProxy,
    size: Size2D<u32>,
    images: Vec<RefCell<VmoImage>>,
    index_map: HashMap<u32, usize>,
}

impl MoldContext {
    pub(crate) fn new(token: ClientEnd<BufferCollectionTokenMarker>, size: Size2D<u32>) -> Self {
        let sysmem = connect_to_service::<AllocatorMarker>().expect("failed to connect to sysmem");
        let (collection_client, collection_request) =
            zx::Channel::create().expect("failed to create Zircon channel");
        sysmem
            .bind_shared_collection(
                ClientEnd::new(token.into_channel()),
                ServerEnd::new(collection_request),
            )
            .expect("failed to bind shared collection");
        let mut buffer_collection = BufferCollectionSynchronousProxy::new(collection_client);
        let mut constraints = buffer_collection_constraints(size.width, size.height);
        buffer_collection
            .set_constraints(true, &mut constraints)
            .expect("failed to set constraints on sysmem buffer");

        Self { buffer_collection, size, images: vec![], index_map: HashMap::new() }
    }
}

impl Context<Mold> for MoldContext {
    fn pixel_format(&self) -> PixelFormat {
        fuchsia_framebuffer::PixelFormat::RgbX888
    }

    fn path_builder(&self) -> Option<MoldPathBuilder> {
        Some(MoldPathBuilder::new())
    }

    fn raster_builder(&self) -> Option<MoldRasterBuilder> {
        Some(MoldRasterBuilder::new())
    }

    fn new_image(&mut self, size: Size2D<u32>) -> MoldImage {
        let image = MoldImage(self.images.len());
        self.images.push(RefCell::new(VmoImage::new(size.width, size.height)));

        image
    }

    fn get_current_image(&mut self, context: &ViewAssistantContext<'_>) -> MoldImage {
        let image_index = context.image_index;

        let buffer_collection = &mut self.buffer_collection;
        let images = &mut self.images;
        let width = self.size.width;
        let height = self.size.height;

        let index = self.index_map.entry(image_index).or_insert_with(|| {
            let index = images.len();
            images.push(RefCell::new(VmoImage::from_buffer_collection(
                buffer_collection,
                width,
                height,
                image_index,
            )));

            index
        });

        MoldImage(*index)
    }

    fn flush_image(&mut self, image: MoldImage) {
        self.images[image.0 as usize].borrow_mut().flush();
    }

    fn render_with_clip(
        &mut self,
        composition: &MoldComposition,
        clip: Rect<u32>,
        image: MoldImage,
        ext: &RenderExt<Mold>,
    ) {
        let image_id = image;
        let mut image = self
            .images
            .get(image.0 as usize)
            .expect(&format!("invalid image {:?}", image_id))
            .borrow_mut();
        let width = self.size.width as usize;
        let height = self.size.height as usize;

        if let Some(PreClear { color }) = ext.pre_clear {
            image.clear([color.b, color.g, color.r, color.a]);
        }

        if let Some(PreCopy { image: src_image_id, copy_region }) = ext.pre_copy {
            let mut src_image = self
                .images
                .get(src_image_id.0 as usize)
                .expect(&format!("invalid PreCopy image {:?}", src_image_id))
                .borrow_mut();

            let src_bytes_per_row = src_image.bytes_per_row();
            let dst_bytes_per_row = image.bytes_per_row();
            let src_slice = src_image.as_mut_slice();
            let dst_slice = image.as_mut_slice();

            copy_region_to_image(
                src_slice,
                width,
                height,
                src_bytes_per_row,
                dst_slice,
                dst_bytes_per_row,
                &copy_region,
            );
        }

        image.render(width, height, composition, clip);

        // TODO: Motion blur support.
        if let Some(PostCopy { image: dst_image_id, copy_region, .. }) = ext.post_copy {
            let mut dst_image = self
                .images
                .get(dst_image_id.0 as usize)
                .expect(&format!("invalid PostCopy image {:?}", dst_image_id))
                .try_borrow_mut()
                .expect(&format!("image {:?} as already used for rendering", dst_image_id));

            let src_bytes_per_row = image.bytes_per_row();
            let dst_bytes_per_row = dst_image.bytes_per_row();
            let src_slice = image.as_mut_slice();
            let dst_slice = dst_image.as_mut_slice();

            copy_region_to_image(
                src_slice,
                width,
                height,
                src_bytes_per_row,
                dst_slice,
                dst_bytes_per_row,
                &copy_region,
            );
        }
    }
}
