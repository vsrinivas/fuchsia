// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{io::Read, mem, slice, sync::Arc};

use anyhow::{ensure, Error};
use fidl_fuchsia_sysmem::{BufferCollectionSynchronousProxy, CoherencyDomain};
use fuchsia_trace::duration;
use fuchsia_zircon::{self as zx, prelude::*};
use fuchsia_zircon_sys as sys;
use mapped_vmo::Mapping;

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
    composition: mold::Composition,
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
            composition: mold::Composition::new(),
            old_layers: None,
            coherency_domain: CoherencyDomain::Cpu,
        }
    }

    pub fn from_png<R: Read>(reader: &mut png::Reader<R>) -> Result<Self, Error> {
        let info = reader.info();
        let color_type = info.color_type;
        ensure!(
            color_type == png::ColorType::RGBA || color_type == png::ColorType::RGB,
            "unsupported color type {:#?}",
            color_type
        );
        let (width, height) = info.size();
        let stride = width as usize * mem::size_of::<u32>();
        let len_bytes = stride * height as usize;
        let (mut mapping, vmo) = mapped_vmo::Mapping::allocate(len_bytes as usize)
            .expect("failed to allocated mapped VMO");
        let (data, len) = mapping.as_ptr_len();
        let slice = unsafe { slice::from_raw_parts_mut(data, len) };
        for dst_row in slice.chunks_mut(stride) {
            let src_row = reader.next_row()?.unwrap();
            // Transfer row and convert to BGRA.
            if color_type == png::ColorType::RGB {
                for (src, dst) in src_row.chunks(3).zip(dst_row.chunks_mut(4)) {
                    dst.copy_from_slice(&[src[2], src[1], src[0], 0xff]);
                }
            } else {
                for (src, dst) in src_row.chunks(4).zip(dst_row.chunks_mut(4)) {
                    dst.copy_from_slice(&[src[2], src[1], src[0], src[3]]);
                }
            }
        }

        Ok(Self {
            vmo,
            width,
            height,
            len_bytes: len_bytes as u64,
            mapping: Arc::new(mapping),
            stride: width as usize,
            composition: mold::Composition::new(),
            old_layers: None,
            coherency_domain: CoherencyDomain::Cpu,
        })
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
            composition: mold::Composition::new(),
            old_layers: None,
            coherency_domain: buffers.settings.buffer_settings.coherency_domain,
        }
    }

    pub fn bytes_per_row(&self) -> usize {
        self.stride * mem::size_of::<u32>()
    }

    pub fn coherency_domain(&self) -> CoherencyDomain {
        self.coherency_domain
    }

    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        let (data, len) = Arc::get_mut(&mut self.mapping).unwrap().as_ptr_len();
        unsafe { slice::from_raw_parts_mut(data, len) }
    }

    pub fn as_buffer(&mut self) -> mold::Buffer<'_> {
        struct SliceFlusher;

        impl mold::Flusher for SliceFlusher {
            fn flush(&self, slice: &mut [[u8; 4]]) {
                unsafe {
                    sys::zx_cache_flush(
                        slice.as_ptr() as *const u8,
                        slice.len() * 4,
                        sys::ZX_CACHE_FLUSH_DATA,
                    );
                }
            }
        }

        let (data, len) = Arc::get_mut(&mut self.mapping).unwrap().as_ptr_len();
        let buffer = unsafe { slice::from_raw_parts_mut(data as *mut [u8; 4], len / 4) };

        mold::Buffer {
            buffer,
            width: self.width as usize,
            width_stride: Some(self.stride),
            flusher: if self.coherency_domain == CoherencyDomain::Ram {
                Some(Box::new(SliceFlusher))
            } else {
                None
            },
        }
    }

    pub fn clear(&mut self, clear_color: [u8; 4]) {
        duration!("gfx", "VmoImage::clear");

        let coherency_domain = self.coherency_domain;

        let (data, len) = Arc::get_mut(&mut self.mapping).unwrap().as_ptr_len();
        let buffer = unsafe { slice::from_raw_parts_mut(data as *mut [u8; 4], len / 4) };

        mold::clear_buffer(buffer, clear_color);

        if coherency_domain == CoherencyDomain::Ram {
            unsafe {
                sys::zx_cache_flush(
                    buffer.as_ptr() as *const u8,
                    buffer.len() * 4,
                    sys::ZX_CACHE_FLUSH_DATA,
                );
            }
        }
    }
}
