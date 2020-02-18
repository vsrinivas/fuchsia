// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{mem, slice, sync::Arc};

use euclid::Rect;
use fidl_fuchsia_sysmem::BufferCollectionSynchronousProxy;
use fuchsia_zircon::{self as zx, prelude::*};
use mapped_vmo::Mapping;

use crate::render::mold::MoldComposition;

#[derive(Clone, Copy, Debug, Eq, Hash, Ord, PartialEq, PartialOrd)]
pub struct MoldImage(pub(crate) usize);

#[derive(Clone, Debug)]
struct ColorBuffer {
    mapping: Arc<Mapping>,
    stride: usize,
}
impl mold::ColorBuffer for ColorBuffer {
    fn pixel_format(&self) -> mold::PixelFormat {
        mold::PixelFormat::BGRA8888
    }

    fn stride(&self) -> usize {
        self.stride
    }

    unsafe fn write_at(&mut self, offset: usize, src: *const u8, len: usize) {
        self.mapping.write_at(offset, std::slice::from_raw_parts(src, len));
    }
}

#[derive(Debug)]
pub(crate) struct VmoImage {
    vmo: zx::Vmo,
    width: u32,
    height: u32,
    len_bytes: u64,
    color_buffer: ColorBuffer,
    map: Option<mold::tile::Map>,
    old_layers: Option<u32>,
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
            color_buffer: ColorBuffer { mapping: Arc::new(mapping), stride: width as usize },
            map: None,
            old_layers: None,
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
            color_buffer: ColorBuffer {
                mapping,
                stride: bytes_per_row as usize / mem::size_of::<u32>(),
            },
            map: None,
            old_layers: None,
        }
    }

    pub fn bytes_per_row(&self) -> usize {
        self.color_buffer.stride * mem::size_of::<u32>()
    }

    pub fn flush(&mut self) {
        self.vmo
            .op_range(zx::VmoOp::CACHE_CLEAN_INVALIDATE, 0, self.len_bytes)
            .expect("failed to clean VMO cache");
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        let (data, len) = Arc::get_mut(&mut self.color_buffer.mapping).unwrap().as_ptr_len();
        unsafe { slice::from_raw_parts_mut(data, len) }
    }

    pub fn clear(&mut self, clear_color: [u8; 4]) {
        let slice = self.as_mut_slice();
        let len = clear_color.len();

        assert_eq!(slice.len() % len, 0);

        for i in 0..slice.len() / len {
            slice[i * len..(i + 1) * len].copy_from_slice(&clear_color);
        }
    }

    pub fn render(
        &mut self,
        width: usize,
        height: usize,
        composition: &MoldComposition,
        clip: Rect<u32>,
    ) {
        self.map = Some(
            self.map
                .take()
                .filter(|map| map.width() == width && map.height() == height)
                .unwrap_or_else(|| mold::tile::Map::without_partial_updates(width, height)),
        );

        let map = self.map.as_mut().unwrap();

        map.set_clip(Some(mold::Clip {
            x: clip.origin.x as usize,
            y: clip.origin.y as usize,
            width: clip.size.width as usize,
            height: clip.size.height as usize,
        }));

        if let Some(layers) = self.old_layers {
            for i in 0..layers {
                map.remove(i);
            }
        }

        self.old_layers = Some(composition.set_up_map(map));

        map.render(self.color_buffer.clone());
    }
}
