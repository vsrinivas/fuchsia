// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{cell::RefCell, collections::HashMap, convert::TryFrom, io::Read, mem, ptr, u32};

use anyhow::Error;
use euclid::{
    default::{Rect, Size2D, Vector2D},
    vec2,
};
use fidl::endpoints::{ClientEnd, ServerEnd};
use fidl_fuchsia_sysmem::{
    AllocatorMarker, BufferCollectionConstraints, BufferCollectionSynchronousProxy,
    BufferCollectionTokenMarker, BufferMemoryConstraints, BufferUsage, CoherencyDomain, ColorSpace,
    ColorSpaceType, FormatModifier, HeapType, ImageFormatConstraints,
    PixelFormat as SysmemPixelFormat, PixelFormatType, CPU_USAGE_WRITE_OFTEN,
    FORMAT_MODIFIER_LINEAR,
};
use fuchsia_component::client::connect_to_protocol;
use fuchsia_framebuffer::{sysmem::set_allocator_name, PixelFormat};
use fuchsia_trace::{duration_begin, duration_end};
use fuchsia_zircon as zx;
use fuchsia_zircon_sys as sys;

use crate::{
    drawing::DisplayRotation,
    render::generic::{
        mold::{
            image::VmoImage, Mold, MoldComposition, MoldImage, MoldPathBuilder, MoldRaster,
            MoldRasterBuilder,
        },
        BlendMode, Context, CopyRegion, Fill, FillRule, GradientType, PostCopy, PreClear, PreCopy,
        RenderExt,
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
        bytes_per_row_divisor: 1,
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
            heap_permitted_count: 0,
            heap_permitted: [HeapType::SystemRam; 32],
        },
        image_format_constraints_count: 1,
        image_format_constraints: [image_format_constraints; 32],
    }
}

fn copy_region_to_image(
    src_ptr: *mut u8,
    src_width: usize,
    src_height: usize,
    src_bytes_per_row: usize,
    dst_ptr: *mut u8,
    dst_len: usize,
    dst_bytes_per_row: usize,
    dst_coherency_domain: CoherencyDomain,
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

            assert!((dst_offset + (columns * 4)) <= dst_len);
            let src = (src_ptr as usize).checked_add(src_offset).unwrap() as *mut u8;
            let dst = (dst_ptr as usize).checked_add(dst_offset).unwrap() as *mut u8;
            unsafe {
                ptr::copy(src, dst, (columns * 4) as usize);
                if dst_coherency_domain == CoherencyDomain::Ram {
                    sys::zx_cache_flush(dst, columns * 4, sys::ZX_CACHE_FLUSH_DATA);
                }
            }

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
    composition: mold::Composition,
    buffer_collection: BufferCollectionSynchronousProxy,
    size: Size2D<u32>,
    display_rotation: DisplayRotation,
    images: Vec<RefCell<VmoImage>>,
    index_map: HashMap<u32, usize>,
}

impl MoldContext {
    pub(crate) fn new(
        token: ClientEnd<BufferCollectionTokenMarker>,
        size: Size2D<u32>,
        display_rotation: DisplayRotation,
    ) -> Self {
        let sysmem = connect_to_protocol::<AllocatorMarker>().expect("failed to connect to sysmem");
        set_allocator_name(&sysmem).unwrap_or_else(|e| eprintln!("set_allocator_name: {:?}", e));
        let (collection_client, collection_request) =
            zx::Channel::create().expect("failed to create Zircon channel");
        sysmem
            .bind_shared_collection(
                ClientEnd::new(token.into_channel()),
                ServerEnd::new(collection_request),
            )
            .expect("failed to bind shared collection");
        let buffer_collection = BufferCollectionSynchronousProxy::new(collection_client);
        let mut constraints = buffer_collection_constraints(size.width, size.height);
        buffer_collection
            .set_constraints(true, &mut constraints)
            .expect("failed to set constraints on sysmem buffer");

        Self {
            composition: mold::Composition::new(),
            buffer_collection,
            size,
            display_rotation,
            images: vec![],
            index_map: HashMap::new(),
        }
    }
}

fn add_to_composition(
    mold_composition: &mut mold::Composition,
    raster: &MoldRaster,
) -> (mold::LayerId, Vector2D<f32>) {
    let mut option = raster.layer_details.borrow_mut();
    let layer_details = option
        .filter(|&(layer_id, layer_translation)| {
            mold_composition.get(layer_id).is_some() && raster.translation == layer_translation
        })
        .unwrap_or_else(|| {
            let layer_id = mold_composition
                .create_layer()
                .unwrap_or_else(|| panic!("Layer limit reached. ({})", u16::max_value()));

            for print in &raster.prints {
                let transform: [f32; 9] = [
                    print.transform.m11,
                    print.transform.m21,
                    print.transform.m31,
                    print.transform.m12,
                    print.transform.m22,
                    print.transform.m32,
                    0.0,
                    0.0,
                    1.0,
                ];
                mold_composition.insert_in_layer_transformed(layer_id, &*print.path, &transform);
            }

            (layer_id, raster.translation)
        });

    *option = Some(layer_details);

    layer_details
}

fn render_composition(
    mold_composition: &mut mold::Composition,
    composition: &MoldComposition,
    buffer: mold::Buffer<'_>,
    display_size: Size2D<u32>,
    display_rotation: DisplayRotation,
    clip: Rect<u32>,
) {
    duration_begin!("gfx", "render::Context<Mold>::clear_layers", "count" => composition.layers.len() as u32);
    for layer in mold_composition.layers_mut() {
        layer.disable();
    }
    duration_end!("gfx", "render::Context<Mold>::clear_layers");

    // mold and Carnelian use different APIs to express path clips:
    // Carnelian has an optional clip field on layers while mold
    // currently requires clips to be added to the layer set and
    // each layer to specify whether or not is clipped by that particular
    // clip.
    //
    // To make this work, Carnelian keeps track of the current clip, adds
    // it to the layer set, then clips any subsequent layers that need to
    // be clipped by the current clip. When a layer comes up with a different
    // clip, the current layer gets reset and the cycle begins anew.

    let mut order_offset = 0;
    let mut current_clip = None;
    let mut layers = composition.layers.iter().rev();

    macro_rules! prints {
        ( $raster:expr ) => {
            // Currently, testing mold rasters for equality forces the
            // underlying prints to the be the same.
            $raster.as_ref().map(|r: &MoldRaster| &r.prints)
        };
    }

    // Enable this to debug partial updates:
    //
    //   buffer.buffer.fill([0, 0, 255, 255]);
    //

    duration_begin!("gfx", "render::Context<Mold>::print_layers", "count" => composition.layers.len() as u32);
    while let Some((order, layer)) = layers.next() {
        if layer.raster.prints.is_empty() {
            continue;
        }

        if prints!(current_clip) != prints!(layer.clip) {
            if let Some(ref first_clip) = layer.clip {
                let layer_details = add_to_composition(mold_composition, first_clip);

                let mold_layer = mold_composition.get_mut(layer_details.0).unwrap();
                let clip_count = 1 + layers
                    .clone()
                    .filter_map(|(_, layer)| layer.clip.as_ref())
                    .take_while(|clip| clip.prints == first_clip.prints)
                    .count();

                let mold_order = u16::try_from(order + order_offset).expect("too many layers");
                mold_layer.enable().set_order(mold_order).set_props(mold::Props {
                    fill_rule: match layer.style.fill_rule {
                        FillRule::NonZero => mold::FillRule::NonZero,
                        FillRule::EvenOdd => mold::FillRule::EvenOdd,
                    },
                    func: mold::Func::Clip(clip_count),
                });

                let transform = display_rotation
                    .transform(&display_size.to_f32())
                    .pre_translate(vec2(layer.raster.translation.x, layer.raster.translation.y));

                mold_layer.set_transform(&[
                    transform.m11,
                    transform.m21,
                    transform.m12,
                    transform.m22,
                    transform.m31,
                    transform.m32,
                ]);

                order_offset += 1;
                current_clip = layer.clip.clone();
            }
        }

        let layer_details = add_to_composition(mold_composition, &layer.raster);

        let mold_layer = mold_composition.get_mut(layer_details.0).unwrap();
        let mold_order = u16::try_from(order + order_offset).expect("too many layers");
        mold_layer.enable().set_order(mold_order).set_props(mold::Props {
            fill_rule: match layer.style.fill_rule {
                FillRule::NonZero => mold::FillRule::NonZero,
                FillRule::EvenOdd => mold::FillRule::EvenOdd,
            },
            func: mold::Func::Draw(mold::Style {
                fill: match &layer.style.fill {
                    Fill::Solid(color) => mold::Fill::Solid(color.to_linear_bgra()),
                    Fill::Gradient(gradient) => {
                        let mut builder = mold::GradientBuilder::new(
                            [gradient.start.x, gradient.start.y],
                            [gradient.end.x, gradient.end.y],
                        );
                        builder.r#type(match gradient.r#type {
                            GradientType::Linear => mold::GradientType::Linear,
                            GradientType::Radial => mold::GradientType::Radial,
                        });

                        for &(color, stop) in &gradient.stops {
                            builder.color_with_stop(color.to_linear_bgra(), stop);
                        }

                        mold::Fill::Gradient(builder.build().unwrap())
                    }
                },
                is_clipped: layer.clip.is_some(),
                blend_mode: match layer.style.blend_mode {
                    BlendMode::Over => mold::BlendMode::Over,
                    BlendMode::Screen => mold::BlendMode::Screen,
                    BlendMode::Overlay => mold::BlendMode::Overlay,
                    BlendMode::Darken => mold::BlendMode::Darken,
                    BlendMode::Lighten => mold::BlendMode::Lighten,
                    BlendMode::ColorDodge => mold::BlendMode::ColorDodge,
                    BlendMode::ColorBurn => mold::BlendMode::ColorBurn,
                    BlendMode::HardLight => mold::BlendMode::HardLight,
                    BlendMode::SoftLight => mold::BlendMode::SoftLight,
                    BlendMode::Difference => mold::BlendMode::Difference,
                    BlendMode::Exclusion => mold::BlendMode::Exclusion,
                    BlendMode::Multiply => mold::BlendMode::Multiply,
                },
                ..Default::default()
            }),
        });

        let transform = display_rotation
            .transform(&display_size.to_f32())
            .pre_translate(vec2(layer.raster.translation.x, layer.raster.translation.y));

        mold_layer.set_transform(&[
            transform.m11,
            transform.m21,
            transform.m12,
            transform.m22,
            transform.m31,
            transform.m32,
        ]);
    }
    duration_end!("gfx", "render::Context<Mold>::print_layers");

    duration_begin!("gfx", "render::Context<Mold>::render_composition");
    mold_composition.render(
        buffer,
        composition.background_color.to_linear_bgra(),
        Some(mold::Rect::new(
            clip.origin.x as usize..(clip.origin.x + clip.size.width) as usize,
            clip.origin.y as usize..(clip.origin.y + clip.size.height) as usize,
        )),
    );
    duration_end!("gfx", "render::Context<Mold>::render_composition");
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
        let buffer_layer_cache = self.composition.create_buffer_layer_cache();
        self.images.push(RefCell::new(VmoImage::new(size.width, size.height, buffer_layer_cache)));

        image
    }

    fn new_image_from_png<R: Read>(
        &mut self,
        reader: &mut png::Reader<R>,
    ) -> Result<MoldImage, Error> {
        let image = MoldImage(self.images.len());
        let buffer_layer_cache = self.composition.create_buffer_layer_cache();
        self.images.push(RefCell::new(VmoImage::from_png(reader, buffer_layer_cache)?));

        Ok(image)
    }

    fn get_image(&mut self, image_index: u32) -> MoldImage {
        let buffer_collection = &mut self.buffer_collection;
        let images = &mut self.images;
        let width = self.size.width;
        let height = self.size.height;
        let composition = &mut self.composition;

        let index = self.index_map.entry(image_index).or_insert_with(|| {
            let index = images.len();
            let buffer_layer_cache = composition.create_buffer_layer_cache();
            images.push(RefCell::new(VmoImage::from_buffer_collection(
                buffer_collection,
                width,
                height,
                image_index,
                buffer_layer_cache,
            )));

            index
        });

        MoldImage(*index)
    }

    fn get_current_image(&mut self, context: &ViewAssistantContext) -> MoldImage {
        self.get_image(context.image_index)
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
            let dst_coherency_domain = image.coherency_domain();
            let dst_slice = image.as_mut_slice();
            let dst_ptr = dst_slice.as_mut_ptr();
            let dst_len = dst_slice.len();
            let dst_bytes_per_row = image.bytes_per_row();

            let src_image = self
                .images
                .get(src_image_id.0 as usize)
                .expect(&format!("invalid PreCopy image {:?}", src_image_id))
                .try_borrow_mut();

            let (src_ptr, src_bytes_per_row) = match src_image {
                Ok(mut image) => (image.as_mut_slice().as_mut_ptr(), image.bytes_per_row()),
                Err(_) => (image.as_mut_slice().as_mut_ptr(), image.bytes_per_row()),
            };

            copy_region_to_image(
                src_ptr,
                width,
                height,
                src_bytes_per_row,
                dst_ptr,
                dst_len,
                dst_bytes_per_row,
                dst_coherency_domain,
                &copy_region,
            );
        }

        render_composition(
            &mut self.composition,
            composition,
            image.as_buffer(),
            self.size,
            self.display_rotation,
            clip,
        );

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
            let dst_coherency_domain = dst_image.coherency_domain();
            let dst_slice = dst_image.as_mut_slice();

            copy_region_to_image(
                src_slice.as_mut_ptr(),
                width,
                height,
                src_bytes_per_row,
                dst_slice.as_mut_ptr(),
                dst_slice.len(),
                dst_bytes_per_row,
                dst_coherency_domain,
                &copy_region,
            );
        }
    }
}
