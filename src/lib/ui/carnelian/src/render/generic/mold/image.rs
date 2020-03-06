// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{mem, slice, sync::Arc, u16};

use euclid::{Rect, Vector2D};
use fidl_fuchsia_sysmem::{BufferCollectionSynchronousProxy, CoherencyDomain};
use fuchsia_trace::duration;
use fuchsia_zircon::{self as zx, prelude::*};
use mapped_vmo::Mapping;

use crate::render::generic::{mold::MoldComposition, BlendMode, Fill, FillRule};

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct MoldImage(pub(crate) usize);

#[derive(Debug)]
pub(crate) struct VmoImage {
    vmo: zx::Vmo,
    width: u32,
    height: u32,
    len_bytes: u64,
    mapping: Arc<Mapping>,
    stride: usize,
    composition: mold_next::Composition,
    old_layers: Option<u32>,
    coherency_domain: CoherencyDomain,
}

impl VmoImage {
    pub fn new(width: u32, height: u32) -> Self {
        let len_bytes = (width * height) as usize * mem::size_of::<u32>();
        let (mapping, vmo) = mapped_vmo::Mapping::allocate(len_bytes as usize)
            .expect("failed to allocated mapped VMO");

        Self {
            vmo,
            width,
            height,
            len_bytes: len_bytes as u64,
            mapping: Arc::new(mapping),
            stride: width as usize,
            composition: mold_next::Composition::new(),
            old_layers: None,
            coherency_domain: CoherencyDomain::Cpu,
        }
    }

    pub fn from_buffer_collection(
        buffer_collection: &mut BufferCollectionSynchronousProxy,
        width: u32,
        height: u32,
        index: u32,
    ) -> Self {
        let (status, buffers) = buffer_collection
            .wait_for_buffers_allocated(zx::Time::after(10.second()))
            .expect("failed to allocate buffer collection");
        assert_eq!(status, zx::sys::ZX_OK);

        let vmo_buffer = &buffers.buffers[index as usize];
        let vmo = vmo_buffer
            .vmo
            .as_ref()
            .expect("failed to get VMO buffer")
            .duplicate_handle(zx::Rights::SAME_RIGHTS)
            .expect("failed to duplicate VMO handle");

        let len_bytes = buffers.settings.buffer_settings.size_bytes;
        let mapping = Arc::new(
            mapped_vmo::Mapping::create_from_vmo(
                &vmo,
                len_bytes as usize,
                zx::VmarFlags::PERM_READ
                    | zx::VmarFlags::PERM_WRITE
                    | zx::VmarFlags::MAP_RANGE
                    | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
            )
            .expect("failed to crate mapping from VMO"),
        );

        assert_eq!(buffers.settings.has_image_format_constraints, true);
        let bytes_per_row = buffers.settings.image_format_constraints.min_bytes_per_row;
        let divisor = buffers.settings.image_format_constraints.bytes_per_row_divisor;
        let bytes_per_row = ((bytes_per_row + divisor - 1) / divisor) * divisor;

        Self {
            vmo,
            width,
            height,
            len_bytes: len_bytes as u64,
            mapping,
            stride: bytes_per_row as usize / mem::size_of::<u32>(),
            composition: mold_next::Composition::new(),
            old_layers: None,
            coherency_domain: buffers.settings.buffer_settings.coherency_domain,
        }
    }

    pub fn bytes_per_row(&self) -> usize {
        self.stride * mem::size_of::<u32>()
    }

    pub fn flush(&mut self) {
        // TODO: avoid flush of whole image by making this part of render().
        if self.coherency_domain != CoherencyDomain::Cpu {
            duration!("gfx", "VmoImage::flush");
            self.vmo
                .op_range(zx::VmoOp::CACHE_CLEAN, 0, self.len_bytes)
                .expect("failed to clean VMO cache");
        }
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        let (data, len) = Arc::get_mut(&mut self.mapping).unwrap().as_ptr_len();
        unsafe { slice::from_raw_parts_mut(data, len) }
    }

    pub fn clear(&mut self, clear_color: [u8; 4]) {
        duration!("gfx", "VmoImage::clear");

        let slice = self.as_mut_slice();
        let len = clear_color.len();

        assert_eq!(slice.len() % len, 0);

        for i in 0..slice.len() / len {
            slice[i * len..(i + 1) * len].copy_from_slice(&clear_color);
        }
    }

    pub fn render(&mut self, composition: &MoldComposition, _clip: Rect<u32>) {
        duration!("gfx", "VmoImage::render");

        // TODO(dtiselice): Use clip.

        let mut current = std::collections::HashSet::new();

        for (order, layer) in composition.layers.iter().rev().enumerate() {
            let layer_id = layer.raster.layer_id.get().unwrap_or_else(|| {
                let layer_id = self
                    .composition
                    .create_layer()
                    .expect(&format!("Layer limit reached. ({})", u16::max_value()));

                for print in &layer.raster.prints {
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
                    self.composition.insert_in_layer_transformed(
                        layer_id,
                        &*print.path,
                        &transform,
                    );
                }

                layer.raster.layer_id.set(Some(layer_id));

                layer_id
            });

            current.insert(layer_id);

            let mold_layer =
                self.composition.get_mut(layer_id).unwrap().set_order(order as u16).set_style(
                    mold_next::Style {
                        fill_rule: match layer.style.fill_rule {
                            FillRule::NonZero => mold_next::FillRule::NonZero,
                            FillRule::EvenOdd => mold_next::FillRule::EvenOdd,
                            // TODO(dtiselice): Implement WholeTile.
                            FillRule::WholeTile => mold_next::FillRule::NonZero,
                        },
                        fill: match layer.style.fill {
                            Fill::Solid(color) => {
                                mold_next::Fill::Solid([color.b, color.g, color.r, color.a])
                            }
                        },
                        blend_mode: match layer.style.blend_mode {
                            BlendMode::Over => mold_next::BlendMode::Over,
                        },
                    },
                );

            if layer.raster.translation != Vector2D::zero() {
                mold_layer.set_transform(&[
                    1.0,
                    0.0,
                    0.0,
                    1.0,
                    layer.raster.translation.x,
                    layer.raster.translation.y,
                ]);
            }
        }

        for layer_id in composition.old_layer_ids.borrow_mut().drain(..) {
            self.composition.remove_layer(layer_id);
        }

        let (data, len) = Arc::get_mut(&mut self.mapping).unwrap().as_ptr_len();
        let buffer = unsafe { slice::from_raw_parts_mut(data as *mut [u8; 4], len / 4) };

        self.composition.render(
            mold_next::Buffer {
                buffer,
                width: self.width as usize,
                width_stride: Some(self.stride),
            },
            [
                composition.background_color.b,
                composition.background_color.g,
                composition.background_color.r,
                composition.background_color.a,
            ],
        );
    }
}
