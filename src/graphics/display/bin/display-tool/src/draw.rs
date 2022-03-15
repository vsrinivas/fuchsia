// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    display_utils::{Image, ImageId},
    fuchsia_image_format::{
        image_format_minimum_row_bytes, image_format_stride_bytes_per_width_pixel,
    },
    fuchsia_zircon as zx,
    mapped_vmo::Mapping,
    std::{cmp::min, convert::TryInto},
};

// TODO(armansito): Extend this to support different patterns and tiled image formats.

pub struct MappedImage {
    image: Image,
    mapping: Mapping,
    pixel_width: u32,
    row_bytes: u32,
}

pub struct Frame {
    pub pos_x: u32,
    pub pos_y: u32,
    pub width: u32,
    pub height: u32,
}

impl MappedImage {
    pub fn create(image: Image) -> Result<MappedImage> {
        let size: usize = image.buffer_settings.size_bytes.try_into()?;
        let mapping = Mapping::create_from_vmo(
            &image.vmo,
            size,
            zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
        )
        .context("failed to map VMO")?;

        let constraints = &image.format_constraints;
        let pixel_width = image_format_stride_bytes_per_width_pixel(&constraints.pixel_format);
        let row_bytes = image_format_minimum_row_bytes(constraints, image.parameters.width)?;
        Ok(MappedImage { image, mapping, pixel_width, row_bytes })
    }

    pub fn id(&self) -> ImageId {
        self.image.id
    }

    pub fn zero(&self) -> Result<()> {
        self.image.vmo.op_range(zx::VmoOp::ZERO, 0, self.image.vmo.get_size()?)?;
        Ok(())
    }

    /// Fill the image with the specified color. The size and interpretation of the given color
    /// must match the underlying image format.
    pub fn fill(&self, color: &[u8]) -> Result<()> {
        self.fill_region(
            color,
            &Frame { pos_x: 0, pos_y: 0, width: self.width(), height: self.height() },
        )
    }

    /// Fill the specified region of this image with the specified color. The size and
    /// interpretation of the given color must match the underlying image format.
    pub fn fill_region(&self, color: &[u8], frame: &Frame) -> Result<()> {
        if self.pixel_width != color.len().try_into()? {
            return Err(format_err!(
                "provided color width ({}) does not match expected pixel format width ({})",
                color.len(),
                self.pixel_width
            ));
        }

        // First clip the frame to the image bounds.
        let frame = {
            let pos_x = min(self.width() - 1, frame.pos_x);
            let pos_y = min(self.height() - 1, frame.pos_y);
            Frame {
                pos_x,
                pos_y,
                width: min(frame.width, self.width() - pos_x),
                height: min(frame.height, self.height() - pos_y),
            }
        };

        // Color the pixels within the frame.
        let pixel_stride = self.row_bytes / self.width();
        for row in 0..frame.height {
            for col in 0..frame.width {
                let idx = self.row_bytes * (row + frame.pos_y) + pixel_stride * (col + frame.pos_x);
                self.mapping.write_at(idx.try_into()?, color);
            }
        }

        Ok(())
    }

    /// Clean (write back) data caches, so previous writes are visible in main memory. This
    /// operation must be called at once before this image buffer gets presented to avoid artifacts
    /// during scanout in height refresh rates if the image is changing frequently. This operation
    /// can be expensive on large images and should be performed sparingly.
    pub fn cache_clean(&self) -> Result<()> {
        self.image.vmo.op_range(zx::VmoOp::CACHE_CLEAN, 0, self.image.vmo.get_size()?)?;
        Ok(())
    }

    fn width(&self) -> u32 {
        self.image.parameters.width
    }

    fn height(&self) -> u32 {
        self.image.parameters.height
    }
}
