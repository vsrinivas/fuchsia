// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Error};
use fidl_fuchsia_sysmem as fsysmem;

use super::linux_drm::DRM_FORMAT_MOD_LINEAR;
use super::round_up_to_increment;

/// The default image format constraints for allocating buffers.
pub const IMAGE_FORMAT_CONSTRAINTS_DEFAULT: fsysmem::ImageFormatConstraints =
    fsysmem::ImageFormatConstraints {
        pixel_format: fsysmem::PixelFormat {
            type_: fsysmem::PixelFormatType::Nv12,
            has_format_modifier: false,
            format_modifier: fsysmem::FormatModifier { value: 0 },
        },
        color_spaces_count: 0,
        color_space: [fsysmem::ColorSpace { type_: fsysmem::ColorSpaceType::Invalid }; 32],
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
        required_min_coded_width: 0,
        required_max_coded_width: 0,
        required_min_coded_height: 0,
        required_max_coded_height: 0,
        required_min_bytes_per_row: 0,
        required_max_bytes_per_row: 0,
    };

/// The default buffers usage for allocating buffers.
pub const BUFFER_USAGE_DEFAULT: fsysmem::BufferUsage =
    fsysmem::BufferUsage { none: 0, cpu: 0, vulkan: 0, display: 0, video: 0 };

/// The default buffer memory constraints for allocating buffers.
pub const BUFFER_MEMORY_CONSTRAINTS_DEFAULT: fsysmem::BufferMemoryConstraints =
    fsysmem::BufferMemoryConstraints {
        min_size_bytes: 0,
        max_size_bytes: u32::MAX,
        physically_contiguous_required: false,
        secure_required: false,
        ram_domain_supported: false,
        cpu_domain_supported: true,
        inaccessible_domain_supported: false,
        heap_permitted_count: 0,
        heap_permitted: [fsysmem::HeapType::SystemRam; 32],
    };

/// The default buffer collection constraints for allocating buffers.
pub const BUFFER_COLLECTION_CONSTRAINTS_DEFAULT: fsysmem::BufferCollectionConstraints =
    fsysmem::BufferCollectionConstraints {
        usage: BUFFER_USAGE_DEFAULT,
        min_buffer_count_for_camping: 0,
        min_buffer_count_for_dedicated_slack: 0,
        min_buffer_count_for_shared_slack: 0,
        min_buffer_count: 0,
        max_buffer_count: 0,
        has_buffer_memory_constraints: false,
        buffer_memory_constraints: BUFFER_MEMORY_CONSTRAINTS_DEFAULT,
        image_format_constraints_count: 0,
        image_format_constraints: [IMAGE_FORMAT_CONSTRAINTS_DEFAULT; 32],
    };

/// Returns the number of bytes per row for a given plane.
///
/// Returns an error if no such number of bytes can be found, either because a number can't be
/// generated from `image_format` or because the `plane` is unsupported.
pub fn get_plane_row_bytes(image_format: &fsysmem::ImageFormat2, plane: u32) -> Result<u32, Error> {
    match plane {
        0 => Ok(image_format.bytes_per_row),
        1 => match image_format.pixel_format.type_ {
            fsysmem::PixelFormatType::Nv12 => Ok(image_format.bytes_per_row),
            fsysmem::PixelFormatType::I420 | fsysmem::PixelFormatType::Yv12 => {
                Ok(image_format.bytes_per_row / 2)
            }
            _ => Err(anyhow!("Invalid pixel format for plane 1.")),
        },
        2 => match image_format.pixel_format.type_ {
            fsysmem::PixelFormatType::I420 | fsysmem::PixelFormatType::Yv12 => {
                Ok(image_format.bytes_per_row / 2)
            }
            _ => Err(anyhow!("Invalid pixel format for plane 2.")),
        },
        _ => Err(anyhow!("Invalid plane.")),
    }
}

/// Returns the byte offset for the given plane.
///
/// Returns an error if the `plane` is unsupported or a valid offset can't be generated from
/// `image_format`.
pub fn image_format_plane_byte_offset(
    image_format: &fsysmem::ImageFormat2,
    plane: u32,
) -> Result<u32, Error> {
    match plane {
        0 => Ok(0),
        1 => match image_format.pixel_format.type_ {
            fsysmem::PixelFormatType::Nv12
            | fsysmem::PixelFormatType::I420
            | fsysmem::PixelFormatType::Yv12 => {
                Ok(image_format.coded_height * image_format.bytes_per_row)
            }
            _ => Err(anyhow!("Invalid pixelformat for plane 1.")),
        },
        2 => match image_format.pixel_format.type_ {
            fsysmem::PixelFormatType::I420 | fsysmem::PixelFormatType::Yv12 => {
                Ok(image_format.coded_height * image_format.bytes_per_row
                    + image_format.coded_height / 2 * image_format.bytes_per_row / 2)
            }
            _ => Err(anyhow!("Invalid pixelformat for plane 2.")),
        },
        _ => Err(anyhow!("Invalid plane.")),
    }
}

/// Returns the linear size for the given `type_`.
///
/// Returns an error if `type_` is unsupported.
pub fn linear_size(
    coded_height: u32,
    bytes_per_row: u32,
    type_: &fsysmem::PixelFormatType,
) -> Result<u32, Error> {
    match type_ {
        fsysmem::PixelFormatType::R8G8B8A8
        | fsysmem::PixelFormatType::Bgra32
        | fsysmem::PixelFormatType::Bgr24
        | fsysmem::PixelFormatType::Rgb565
        | fsysmem::PixelFormatType::Rgb332
        | fsysmem::PixelFormatType::Rgb2220
        | fsysmem::PixelFormatType::L8
        | fsysmem::PixelFormatType::R8
        | fsysmem::PixelFormatType::R8G8
        | fsysmem::PixelFormatType::A2B10G10R10
        | fsysmem::PixelFormatType::A2R10G10B10 => Ok(coded_height * bytes_per_row),
        fsysmem::PixelFormatType::I420 => Ok(coded_height * bytes_per_row * 3 / 2),
        fsysmem::PixelFormatType::M420 => Ok(coded_height * bytes_per_row * 3 / 2),
        fsysmem::PixelFormatType::Nv12 => Ok(coded_height * bytes_per_row * 3 / 2),
        fsysmem::PixelFormatType::Yuy2 => Ok(coded_height * bytes_per_row),
        fsysmem::PixelFormatType::Yv12 => Ok(coded_height * bytes_per_row * 3 / 2),
        _ => Err(anyhow!("Invalid pixel format.")),
    }
}

/// Converts a `fsysmem::ImageFormatConstraints` to an `fsysmem::ImageFormat2`.
pub fn constraints_to_format(
    constraints: &fsysmem::ImageFormatConstraints,
    coded_width: u32,
    coded_height: u32,
) -> Result<fsysmem::ImageFormat2, Error> {
    if coded_width < constraints.min_coded_width
        || (constraints.max_coded_width > 0 && coded_width > constraints.max_coded_width)
    {
        return Err(anyhow!("Coded width not within constraint bounds."));
    }
    if coded_height < constraints.min_coded_height
        || (constraints.max_coded_height > 0 && coded_height > constraints.max_coded_height)
    {
        return Err(anyhow!("Coded height not within constraint bounds."));
    }

    let format = fsysmem::ImageFormat2 {
        pixel_format: constraints.pixel_format,
        coded_width,
        coded_height,
        bytes_per_row: image_format_minimum_row_bytes(constraints, coded_width).unwrap_or(0),
        display_width: coded_width,
        display_height: coded_height,
        layers: 0,
        color_space: if constraints.color_spaces_count > 0 {
            constraints.color_space[0]
        } else {
            fsysmem::ColorSpace { type_: fsysmem::ColorSpaceType::Invalid }
        },
        has_pixel_aspect_ratio: false,
        pixel_aspect_ratio_width: 0,
        pixel_aspect_ratio_height: 0,
    };

    Ok(format)
}

/// Returns the minimum row bytes for the given constraints and width.
///
/// Returns an error if the width is invalid given the constraints, or the constraint pixel format
/// modifier is invalid.
pub fn image_format_minimum_row_bytes(
    constraints: &fsysmem::ImageFormatConstraints,
    width: u32,
) -> Result<u32, Error> {
    if constraints.pixel_format.format_modifier.value != DRM_FORMAT_MOD_LINEAR {
        return Err(anyhow!("Non-linear format modifier."));
    }
    if width < constraints.min_coded_width
        || (constraints.max_coded_width > 0 && width > constraints.max_coded_width)
    {
        return Err(anyhow!("Width outside of constraints."));
    }

    let constraints_min_bytes_per_row = constraints.min_bytes_per_row;
    let constraints_bytes_per_row_divisor = constraints.bytes_per_row_divisor;

    round_up_to_increment(
        std::cmp::max(
            image_format_stride_bytes_per_width_pixel(&constraints.pixel_format) * width,
            constraints_min_bytes_per_row,
        ) as usize,
        constraints_bytes_per_row_divisor as usize,
    )
    .map(|bytes| bytes as u32)
}

pub fn image_format_stride_bytes_per_width_pixel(pixel_format: &fsysmem::PixelFormat) -> u32 {
    match pixel_format.type_ {
        fsysmem::PixelFormatType::Invalid
        | fsysmem::PixelFormatType::Mjpeg
        | fsysmem::PixelFormatType::DoNotCare => 0,
        fsysmem::PixelFormatType::R8G8B8A8 => 4,
        fsysmem::PixelFormatType::Bgra32 => 4,
        fsysmem::PixelFormatType::Bgr24 => 3,
        fsysmem::PixelFormatType::I420 => 1,
        fsysmem::PixelFormatType::M420 => 1,
        fsysmem::PixelFormatType::Nv12 => 1,
        fsysmem::PixelFormatType::Yuy2 => 2,
        fsysmem::PixelFormatType::Yv12 => 1,
        fsysmem::PixelFormatType::Rgb565 => 2,
        fsysmem::PixelFormatType::Rgb332 => 1,
        fsysmem::PixelFormatType::Rgb2220 => 1,
        fsysmem::PixelFormatType::L8 => 1,
        fsysmem::PixelFormatType::R8 => 1,
        fsysmem::PixelFormatType::R8G8 => 2,
        fsysmem::PixelFormatType::A2B10G10R10 => 4,
        fsysmem::PixelFormatType::A2R10G10B10 => 4,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_linear_row_bytes() {
        let linear = fsysmem::PixelFormat {
            type_: fsysmem::PixelFormatType::Bgra32,
            has_format_modifier: true,
            format_modifier: fsysmem::FormatModifier { value: fsysmem::FORMAT_MODIFIER_LINEAR },
        };

        let constraints = fsysmem::ImageFormatConstraints {
            pixel_format: linear,
            min_coded_width: 12,
            max_coded_width: 100,
            bytes_per_row_divisor: 4 * 8,
            max_bytes_per_row: 100000,
            ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
        };

        assert_eq!(image_format_minimum_row_bytes(&constraints, 17).unwrap(), 4 * 24);

        assert!(image_format_minimum_row_bytes(&constraints, 11).is_err());
        assert!(image_format_minimum_row_bytes(&constraints, 101).is_err());
    }

    #[test]
    fn plan_byte_offset() {
        let mut linear = fsysmem::PixelFormat {
            type_: fsysmem::PixelFormatType::Bgra32,
            has_format_modifier: true,
            format_modifier: fsysmem::FormatModifier { value: fsysmem::FORMAT_MODIFIER_LINEAR },
        };

        let constraints = fsysmem::ImageFormatConstraints {
            pixel_format: linear,
            min_coded_width: 12,
            max_coded_width: 100,
            min_coded_height: 12,
            max_coded_height: 100,
            bytes_per_row_divisor: 4 * 8,
            max_bytes_per_row: 100000,
            ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
        };

        let image_format = constraints_to_format(&constraints, 18, 17).unwrap();
        // The raw size would be 72 without bytes_per_row_divisor of 32.
        assert_eq!(image_format.bytes_per_row, 96);

        assert_eq!(image_format_plane_byte_offset(&image_format, 0).unwrap(), 0);
        assert!(image_format_plane_byte_offset(&image_format, 1).is_err());

        linear.type_ = fsysmem::PixelFormatType::I420;
        let constraints = fsysmem::ImageFormatConstraints {
            pixel_format: linear,
            min_coded_width: 12,
            max_coded_width: 100,
            min_coded_height: 12,
            max_coded_height: 100,
            bytes_per_row_divisor: 4 * 8,
            max_bytes_per_row: 100000,
            ..IMAGE_FORMAT_CONSTRAINTS_DEFAULT
        };

        const BYTES_PER_ROW: u32 = 32;
        let image_format = constraints_to_format(&constraints, 18, 20).unwrap();
        assert_eq!(image_format.bytes_per_row, BYTES_PER_ROW);

        assert_eq!(image_format_plane_byte_offset(&image_format, 0).unwrap(), 0);
        assert_eq!(image_format_plane_byte_offset(&image_format, 1).unwrap(), BYTES_PER_ROW * 20);
        assert_eq!(
            image_format_plane_byte_offset(&image_format, 2).unwrap(),
            BYTES_PER_ROW * 20 + BYTES_PER_ROW / 2 * 20 / 2
        );
        assert!(image_format_plane_byte_offset(&image_format, 3).is_err());

        assert_eq!(get_plane_row_bytes(&image_format, 0).unwrap(), BYTES_PER_ROW);
        assert_eq!(get_plane_row_bytes(&image_format, 1).unwrap(), BYTES_PER_ROW / 2);
        assert_eq!(get_plane_row_bytes(&image_format, 2).unwrap(), BYTES_PER_ROW / 2);
        assert!(get_plane_row_bytes(&image_format, 3).is_err())
    }
}
