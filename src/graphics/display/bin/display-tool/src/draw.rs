// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Result},
    display_utils::Image,
    fuchsia_image_format::image_format_stride_bytes_per_width_pixel as pixel_width,
    fuchsia_zircon as zx,
    mapped_vmo::Mapping,
    std::convert::TryInto,
};

// TODO(armansito): Extend this to support different patterns and tiled image formats.

/// Fill the given image with the specified color. The size and interpretation of the given color
/// must match the provided image format.
pub(crate) fn fill(image: &Image, color: &[u8]) -> Result<()> {
    let format_width: usize = pixel_width(&image.format_constraints.pixel_format).try_into()?;
    if format_width != color.len() {
        return Err(format_err!(
            "provided color width ({}) does not match expected pixel format width ({})",
            color.len(),
            format_width
        ));
    }

    let mapping_size: usize = image.buffer_settings.size_bytes.try_into()?;
    let mapping = Mapping::create_from_vmo(
        &image.vmo,
        mapping_size,
        zx::VmarFlags::PERM_READ | zx::VmarFlags::PERM_WRITE,
    )
    .context("failed to map VMO")?;

    for i in 0..(mapping_size / format_width) {
        mapping.write_at(i * format_width, color);
    }
    image.vmo.op_range(zx::VmoOp::CACHE_CLEAN_INVALIDATE, 0, image.vmo.get_size()?)?;

    Ok(())
}
